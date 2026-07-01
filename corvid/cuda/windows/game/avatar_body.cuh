// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <cmath>

#include "../../vec.cuh"

// The avatar's physical body: a rigid ball that rolls on terrain under
// gravity, driven by a traction force at its contact.
//
// This is  a real rigid body whose motion comes from forces, replacing the
// velocity-easing rig the game ships today. It is host math with no CUDA
// dependency, so it can be unit-tested on its own.
//
// The terrain reaches it only through `body_contact`, the seam: the game fills
// that from the GPU ground probe, the tests fill it from synthetic floors,
// ramps, and walls, and keystone B (a true distance field) later swaps a
// better contact in without touching the dynamics here.
//
// The body owns angular velocity as a real wheel-spin DOF, so "spinning the
// ball" and "what the spin does" are distinct. On a floor, the contact
// friction couples the spin to travel, but only up to the friction budget: a
// drive that outruns the budget (a steep dig wall, ice) spins the wheel faster
// than the ball moves, real over-spin the tread reads as slip. Airborne, the
// wheel revs free toward the command. The drawn wireframe roll and the
// steer-tread fudge stay in `avatar_rig`, reading this body's outputs.

namespace corvid::cuda {

#pragma region avatar_body

// World up; gravity pulls along its negative.
inline constexpr vec3 body_up{0.0F, 1.0F, 0.0F};

// The ball's physical constants.
//
// Drive and friction are kept as a force and a coefficient. Acceleration is
// the drive force over the mass, traction is bounded by the contact's
// `friction * normal load`, and gravity supplies the rest. A quadratic `drag`
// (force ~ speed squared) then sets the terminal cruise speed, v =
// sqrt(drive_acceleration / drag), so a larger drive (Run) settles higher
// super-linearly, and `jump_up` blends the jump between straight up and the
// contact normal.
//
// For Run to read as faster, walk must sit below the traction ceiling
// (`friction * gravity`) so Run has headroom; raise `friction` until it does.
// Some of these may surface in the tuning panel and some may not; the body
// does not mirror the config UI.
struct body_params {
  float radius = 1.0F;       // sphere radius
  float mass = 1.0F;         // contact normal load and rotational inertia
  float gravity = 20.0F;     // downward acceleration
  float drive_force = 50.0F; // traction force commanded at full walk
  float friction = 8.0F;     // contact friction coefficient (mu)
  float drag = 0.2F;         // quadratic resistance (~speed^2); sets cruise
  float jump_speed = 18.0F;  // launch speed on a grounded jump
  float jump_up = 0.5F;      // jump dir blend: 1 = up, 0 = contact normal
  float rolling_resistance = 15.0F; // constant coasting brake (0 rolls on)

  // Moment of inertia of a uniform solid sphere, 2/5 m r^2.
  [[nodiscard]] float inertia() const { return 0.4F * mass * radius * radius; }

  // The traction budget for a normal `load`: the most tangential acceleration
  // friction can supply (mu times the load). Worked as an acceleration, so the
  // mass cancels, like `load` itself.
  [[nodiscard]] float max_traction(float load) const {
    return friction * load;
  }

  // How much faster the motor's free-wheel spin-up runs than the rolling
  // spin-up for the same traction (1 + m r^2 / I; 7/2 for a solid sphere). It
  // sets both how fast the wheel over-spins past rolling when the drive beats
  // the friction budget and how fast contact friction kills that slip back, so
  // the over-spin balances the budget through one shared coefficient.
  [[nodiscard]] float spin_coupling() const {
    return (inertia() + (mass * radius * radius)) / inertia();
  }
};

// One step's contact with the terrain: the seam between the body and the
// world.
//
// `touching` says whether the ball met any surface this step (floor, wall, or
// ceiling) rather than hanging in the air; when it is false the other fields
// are unused. `normal` is the unit outward direction from the surface toward
// the ball, and `penetration` is how far the ball has sunk past resting
// (radius minus the surface distance), positive when overlapping.
//
// `is_floor()` narrows `touching` to an upward-facing surface (a ramp counts,
// up to the vertical limit, since the test is only that the normal still
// points up), the one the body drives and jumps on; the body's own `grounded`
// narrows it further still to resting there and not rising off it.
//
// Two deliberate simplifications live here, worth revisiting if they bite.
// "Floor" is the gravity-aligned `normal.y > 0`, a crude cut with an arbitrary
// boundary near vertical. And the traction budget's normal load comes from
// gravity alone (`normal_load`), so traction is floor-only: a sphere is not a
// tire and in principle grips at any contact given something to press it
// there, and that presser need not be gravity.
//
// A wedge or chimney brace could load a wall; so could the ball's own inertia,
// the centripetal reaction ("centrifugal force") of following a curved track
// standing in for gravity, so a fast enough ball could run a loop and keep
// traction at the top, where the normal points down. We model none of it: the
// load is gravity's, so a wall or ceiling carries no traction and only stops
// motion into it.
struct body_contact {
  bool touching{};
  vec3 normal{};
  float penetration{};

  // A surface the ball can stand and gain traction on: in contact and facing
  // upward, as opposed to a wall or ceiling.
  [[nodiscard]] bool is_floor() const { return touching && normal.y > 0.0F; }

  // The speed `v` is closing into the surface: positive when penetrating,
  // negative when separating (rising off it).
  [[nodiscard]] float into(vec3 v) const { return -dot(v, normal); }

  // `v` resolved onto the contact plane: its component along the surface, with
  // the part into or out of the surface removed.
  [[nodiscard]] vec3 tangent(vec3 v) const { return reject(v, normal); }

  // The inward acceleration the surface must support against `accel` (g cos
  // theta for gravity on a slope), as an acceleration so the mass cancels in
  // the traction budget; clamped to nonnegative.
  [[nodiscard]] float normal_load(vec3 accel) const {
    return fmaxf(0.0F, -dot(accel, normal));
  }
};

// The rigid ball: its kinematic state plus the step that advances it.
struct avatar_body {
#pragma region State

  pos3 center{};           // ball center
  vec3 velocity{};         // linear velocity (world)
  vec3 angular_velocity{}; // spin (world); real state, not derived from travel
  bool grounded{};         // resting on a floor (can jump, has traction)
  body_params params{};

#pragma endregion
#pragma region Step

  // Advance the body one step against `contact`.
  //
  // `drive` is the commanded traction, in world space, as a fraction of
  // `drive_force`; the contact flattens it onto the surface. The rig sets it
  // from the movement input, and Walk and Run both command traction through
  // it.
  //
  // The force is friction-limited in `drive_on_floor`: a command the surface's
  // friction budget (its coefficient and slope) cannot hold skids instead of
  // accelerating the ball, in Walk or Run alike, while good ground grips both.
  // `jump` launches off a floor contact, in a `jump_up` blend of straight up
  // and the contact normal. The order is force, then integrate (semi-implicit
  // Euler).
  void advance(const body_contact& contact, vec3 drive, bool jump, float dt) {
    const vec3 gravity = body_up * -params.gravity;
    const bool floor = contact.is_floor();

    // Jump-ready: on a floor and not rising off it (descending into the
    // contact, or at rest on it, where the resolve holds it). `jump` is a held
    // request, so a press while the ball is bouncing up waits for it to fall
    // back to the next contact rather than firing in mid-air; that is what
    // lets a hold hop off each landing and a tap jump once. A rising ball is
    // either between hops or climbing out right after one (where the stale
    // probe still reads `floor`), and a ball off the ground has no floor at
    // all, so the not-rising sign alone guards both without a tuned velocity
    // threshold.
    const bool jump_ready = floor && contact.into(velocity) >= 0.0F;

    // A jump is an impulse in a blend of straight up and the contact normal
    // (`jump_up`): up uses the droid's propulsion to leap regardless of the
    // ground, the normal pushes off the surface (off a steep face, or up and
    // out of a pit).
    if (jump && jump_ready) {
      const vec3 dir = normalize(
          (body_up * params.jump_up) +
          (contact.normal * (1.0F - params.jump_up)));
      velocity += dir * params.jump_speed;
    }

    velocity += gravity * dt;

    if (contact.touching) resolve_contact(contact);
    if (floor)
      drive_on_floor(contact, gravity, drive, dt); // also spins the wheel
    else
      spin_in_air(drive, dt);

    // Resting support. The engine's floor probe is one frame stale, so a
    // settled ball sinks a hair under gravity each frame and the next frame's
    // resolve reseats it, a small vertical sawtooth: invisible outside, but
    // the merged glass lens magnifies it into a shimmer.
    //
    // When the ball is essentially seated on a floor and not moving sideways,
    // cancel the into-floor velocity gravity just added, so the contact
    // supports it statically with no sink.
    //
    // Gated two ways. The seated band (`seat_band`) leaves a ball still
    // falling from higher up to seat under gravity as before. Near-zero planar
    // speed keeps this in-band cancel from firing the tilted-normal upward
    // kick that sideways motion drove (see `resolve_contact`); a moving ball
    // still bobs, but its motion hides it.
    if (floor && contact.penetration > -seat_band &&
        length(contact.tangent(velocity)) < rest_speed)
      if (const float closing = contact.into(velocity); closing > 0.0F)
        velocity += contact.normal * closing;

    center += velocity * dt;

    // Grounded (can jump, has traction) when resting on a floor and not rising
    // off it faster than the band.
    grounded = floor && contact.into(velocity) >= -contact_eps;
  }

#pragma endregion
#pragma region Resolve
private:
  static constexpr float contact_eps = 1.0e-3F; // "resting, not rising" band
  static constexpr float tiny = 1.0e-6F;        // divide-by-length guard
  // Resting support (see `advance`): the seated penetration band the support
  // holds the ball in, and the planar speed below which a floor contact counts
  // as at rest.
  static constexpr float seat_band = 0.008F;
  static constexpr float rest_speed = 0.5F;

  // Push the ball out of any overlap and, once it is in real contact, cancel
  // motion into the surface (a dead stop, no bounce). Runs for floors and
  // walls alike.
  //
  // The cancel fires only at actual contact (`penetration >= 0`), not across
  // the whole `touching` span. The engine widens `touching` by a tolerance
  // band (`ground_tol`) so the grounded flag stays steady over a stale, sparse
  // probe, but that band must not push on a ball still hovering above the
  // surface. Cancelling in the band pinned the ball at the band's top edge
  // instead of letting it seat, and with a slightly tilted normal under
  // sideways motion the cancel injected a small upward kick every frame there,
  // pumping a limit cycle that lofted the ball clear of the band and strobed
  // the ground contact. Returning while hovering lets gravity seat the ball at
  // the surface (`penetration` ~ 0), where it rests steadily inside the band.
  void resolve_contact(const body_contact& contact) {
    if (contact.penetration < 0.0F)
      return; // hovering in the band: let it seat
    if (contact.penetration > 0.0F)
      center += contact.normal * contact.penetration;
    if (const float closing = contact.into(velocity); closing > 0.0F)
      velocity += contact.normal * closing;
  }

  // Apply the traction-limited drive and the static-friction slope hold, both
  // in the contact's tangent plane.
  //
  // Traction is bounded by `friction * normal load`: a drive past that budget
  // delivers only the budget (the rest is wheel-spin). Whatever budget the
  // drive leaves, static friction spends holding the downhill pull, so the
  // ball holds on a slope up to the friction angle (tan theta == mu) and
  // slides above it, replacing the old hardcoded climb-angle cutoff.
  void drive_on_floor(const body_contact& contact, vec3 gravity, vec3 drive,
      float dt) {
    const float fric_max = params.max_traction(contact.normal_load(gravity));

    const vec3 force = drive * params.drive_force;
    const vec3 drive_cmd = contact.tangent(force) * (1.0F / params.mass);
    vec3 drive_acc = drive_cmd;
    if (const float mag = length(drive_acc); mag > fric_max && mag > tiny)
      drive_acc *= fric_max / mag;
    velocity += drive_acc * dt;

    const vec3 down = contact.tangent(gravity); // downhill acceleration
    const float remaining = fmaxf(0.0F, fric_max - length(drive_acc));
    if (const float mag = length(down); mag > tiny) {
      const float held = fminf(mag, remaining);
      velocity -= (down * (1.0F / mag)) * (held * dt);
    }

    // Quadratic drag (force proportional to speed squared, the real regime)
    // sets the terminal cruise speed: drive balances it at v =
    // sqrt(drive_acceleration / drag), so a larger drive (Run) settles higher
    // super-linearly. Integrated analytically as v / (1 + drag*v*dt), the
    // exact solution of dv/dt = -drag*v^2, so it is stable at any dt or speed.
    // Floor only, so a jump arc stays ballistic.
    if (params.drag > 0.0F) {
      const vec3 vt = contact.tangent(velocity);
      if (const float speed = length(vt); speed > tiny)
        velocity -= vt * (1.0F - (1.0F / (1.0F + (params.drag * speed * dt))));
    }

    // Rolling resistance brakes a coasting ball to rest; a perfect ball would
    // roll on forever, so this is the small real loss that stops it.
    if (params.rolling_resistance > 0.0F) {
      const vec3 vt = contact.tangent(velocity);
      if (const float mag = length(vt); mag > tiny) {
        const float brake = fminf(mag, params.rolling_resistance * dt);
        velocity -= (vt * (1.0F / mag)) * brake;
      }
    }

    // The wheel spin is coupled last, against the settled velocity.
    spin_on_floor(contact, drive_cmd, fric_max, dt);
  }

  // Evolve the wheel-spin DOF on a floor.
  //
  // The wheel carries real angular velocity, coupled to the ground by the same
  // friction budget that bounds the drive. The motor spins it up by the full
  // (unclamped) command; the contact friction kills the slip, the wheel speed
  // past the rolling speed, back toward rolling, bounded by the budget. A
  // command within the budget is absorbed whole, so the wheel rolls (no slip);
  // the excess of a command that beats the budget survives as real over-spin,
  // the slip the tread shows.
  //
  // Drag caps the rev so a wheel spinning free against a wall does not run
  // away. The spin reads `velocity` but never writes it: the excess angular
  // momentum is lost to spin, which is exactly why the ball is stuck.
  void spin_on_floor(const body_contact& contact, vec3 drive_cmd,
      float fric_max, float dt) {
    const float r = params.radius;
    const float k = params.spin_coupling();
    const vec3 omega_roll = cross(contact.normal, velocity) * (1.0F / r);
    vec3 slip = angular_velocity - omega_roll;

    // The motor spins the wheel up by the unclamped command.
    slip += cross(contact.normal, drive_cmd) * (k * dt / r);

    // Contact friction kills the slip toward zero, capped by the budget;
    // static friction locks it at zero once the slip is spent.
    if (const float kill = k * fric_max * dt / r, s = length(slip); s > kill)
      slip *= (s - kill) / s;
    else
      slip = vec3{};

    // Quadratic drag caps the rev (reuses the translation drag).
    if (params.drag > 0.0F)
      if (const float s = length(slip); s > tiny)
        slip *= 1.0F / (1.0F + (params.drag * s * r * dt));

    angular_velocity = omega_roll + slip;
  }

  // Rev the free wheel in the air toward the command, about the heading plane,
  // so flooring it off the ground spins the tread up (the airborne face of
  // slip); with no command it coasts. Drag caps the rev. On landing the gap
  // between this spin and the new rolling rate shows as slip until the contact
  // kills it.
  void spin_in_air(vec3 drive, float dt) {
    const float r = params.radius;
    const vec3 force = drive * params.drive_force;
    const vec3 drive_cmd = reject(force, body_up) * (1.0F / params.mass);
    angular_velocity +=
        cross(body_up, drive_cmd) * (params.spin_coupling() * dt / r);
    if (params.drag > 0.0F)
      if (const float s = length(angular_velocity); s > tiny)
        angular_velocity =
            angular_velocity * (1.0F / (1.0F + (params.drag * s * r * dt)));
  }

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda

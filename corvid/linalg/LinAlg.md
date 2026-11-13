# Linear Algebra notes

Based on the exciting [**Essence of Linear Algebra**](https://www.youtube.com/playlist?list=PLZHQObOWTQDPD3MizzM2xVFitgF8hE_ab) series by 3Blue1Brown.

## Vectors

- A geometric vector has magnitude and direction, independent of its location or coordinate representation.
  - Technically, a zero vector, $\vec{0}$, has no direction because its magnitude is 0.
  - More generally, a mathematical vector is an element of a vector space, defined by the operations of vector addition and scalar multiplication.
  - This is the mathematical abstraction that's independent of any specific physical context or coordinate system.
- A position vector describes a position relative to an origin.
  - The vector starts at a fixed reference point, such as $O(0,0,0)$ and points to a specific point in space, $P(x,y,z)$.
  - Often denoted as $\vec{r} = x\hat{i} + y\hat{j} + z\hat{k}$.
  - Since it's anchored to the origin, changing the origin also changes the vector.
- A displacement vector describes a change in position, independent of the starting point.
  - It represents the straight-line distance and direction (as opposed to what was actually traveled) from an initial point to a final position.
  - Mathematically, it's the difference between two position vectors: $\Delta \vec{r} = \vec{r}_{\text{final}} - \vec{r}_{\text{initial}}$.
- A free vector is defined only by magnitude and direction, with no fixed point of application or line of action in space.
  - You can translate a free vector anywhere in space (keeping it parallel to itself) without changing its mathematical meaning.
  - Pure translations and velocities are often treated as free vectors in physics.
- Tensor, in numerical computing, is often used as a general term for multidimensional arrays, including scalars, vectors, and matrices. Mathematical tensors are a more specific concept.
- Array rank, in numerical computing, refers to the number of dimensions or axes an array has, which determines its structure.
  - Rank 0: scalar
  - Rank 1: vector
  - Rank 2: matrix
  - Rank 3 or higher: higher-rank tensor
  - All of these can be represented using LibTorch's `at::Tensor`, whose `dim()` method returns its number of axes.
- Array rank should not be confused with matrix rank.
  - Matrix rank is the number of linearly independent rows or columns it contains, which can be computed with `at::linalg_matrix_rank()` by using Gaussian elimination.
  - The the number of linearly independent rows always equals the number of linearly independent columns.

### How they're seen in the fields of physics, math, and computer science 

- Obligatory joke:
  - The computer scientist uses math to model reality.
  - The physicist thinks reality models math.
  - The mathematician has yet to make any connection between the two.

- Physics often visualizes vectors as arrows, representing magnitude and direction.
  - Some are free vectors, hence defined entirely by that magnitude and direction.
    - These are the ones 3Blue1Brown emphasizes for physics.
  - Others are associated with specific positions or points of application, hence they are additionally defined by an origin.
  - Typically used to model physical spacetime, where it's limited to 1, 2, 3, or 4 dimensions.
- Math sees it generally and operationally.
  - Geometric vectors, which 3Blue1Brown starts with, are almost always rooted at the origin, but math is more general than that.
  - Must have a sensible notion of:
    - Adding two vectors.
    - Multiplying a vector by a number.

- Computer science sees it as a data structure containing an ordered list of elements, each of which is typically a number.
  - There's a lot of confusion between "vector" as the `std::vector` array data structure and the vector as the mathematical object it's used to represent.
    - A `std::vector` is 1-dimensional and can have any number of elements.
    - Independently, each element can be seen as referring to a dimension of a mathematical vector.
    - As a result, we represent mathematical vectors with arbitrarily many dimensions as a single list of values, one per dimension.
    - To reiterate, array dimensionality (which is about the data structure) and vector-space dimensionality (which is about the mathematical vectors being represented by it) are completely different concepts.
    - It's probably best to use "array" to refer to the various data structures of various ranks and reserve "vector" for the mathematical concept. So a `std::vector` is just a 1D array, which can be used to encode a vector with more dimensions.
  - A `std::vector` is typically viewed as 1-dimensional, but we can also have higher-dimensional views over it, forming a 2-dimensional matrix or an N-dimensional tensor.
    - This is based on something like `std::mdspan` with its row-first view, as opposed to nested vectors, aka (potentially-)jagged arrays, which are a different data structure.
    - The consequence is that the array data structure is often limited to a few dimensions, even if its values represent many.
    - A "3-dimensional vector" is represented by a 1-dimensional `std::vector` with 3 elements, not a 3D view over it.
  - Not mentioned, but polar coordinates also make sense: the magnitude and angle(s) map directly to the coordinates at the tip, with a bit of trig. This is not the representation we typically use.
    - This representation is equivalent, and has its own advantages and disadvantages, such as making it easier to see the magnitude and angles at a glance. However, the various formulas here apply to only to Cartesian coordinates, and the calculation can be more complex and ambiguous for polars.
    - Note that, to store a 3-dimensional vector, we'd still need three elements: the magnitude, and the angles within the two planes. However, they're now two kinds of values, which complicates matters.
    - There's also an ambiguity introduced, which is that a magnitude of 0 (in a zero vector, $\vec{0}$) leaves the angles undefined. And there can be nonunique angles at the poles.

- 3Blue1Brown wants to initially emphasize:
  - A coordinate system.
  - An arrow sitting on the origin at its center.

## Coordinates vs. vectors

- A vector rooted at the origin is defined by the coordinate of its tip.
  - Instead of $(x, y)$ for the coordinate representing the location, we use $\begin{bmatrix} x \\ y \end{bmatrix}$ for the vector representing the displacement.
  - Note that this is a column vector, which is not interchangeable with a row vector such as $\begin{bmatrix} x & y \end{bmatrix}$.
- An orthogonal Cartesian coordinate system is a set of orthogonal number lines, defining a grid.
- Adding vectors is just component-wise or member-wise: $$\begin{bmatrix} x_1 \\ y_1 \end{bmatrix} + \begin{bmatrix} x_2 \\ y_2 \end{bmatrix} = \begin{bmatrix} x_1+x_2 \\ y_1+y_2 \end{bmatrix}$$
  - Visually, it's equivalent to moving the tail of the second vector to the tip of the first, then drawing an arrow from the origin to the new tip.
- Multiplying a vector by a scalar is also member-wise: $$s \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} sx \\ sy \end{bmatrix} $$
  - The vector is being scaled by the number, which is why we call it a scalar.
    - We also use this term for a 0-dimensional data structure.
  - This scales its magnitude without changing its direction.
    - Unless the scalar is negative, in which case it points in the opposite direction.
    - Either way, it remains parallel to the original.
  - In the degenerate case, multiplying by a scalar of 0 produces the zero vector, $\vec{0}$, which has no direction defined.

## Linear combinations, span, and basis

### Basis

- Each of the coordinates is a sort of scalar because it can be seen as scaling a unit vector.
  - $\hat{i}$, $\hat{j}$, and $\hat{k}$ are unit vectors for $x$, $y$, and $z$.
    - They form the basis for the $xyz$ coordinate system, so they're called the basis vectors.
  - This is how we got from $\begin{bmatrix} x \\ y \\ z \end{bmatrix}$ to  $x\hat{i} + y\hat{j} + z\hat{k}$, for a position vector.
 - There's nothing special about the basis we chose. Any three orthogonal vectors can serve as a basis for 3D space.
   - They don't even have to be orthogonal, just linearly independent, although orthogonal bases make the mathematics easier.
   - A different basis would lead to different values for the coordinates, but there's a mathematical relationship that allows us to interconvert.
- The technical definition: the basis of a vector space is a set of linearly independent vectors that span the full space.

### Linear combinations

- The combination of adding vectors while scaling them is called linear combination.
  - Why? Because it's linear algebra so "linear" is the brand name. It's like MS Visual Studio being called "Visual".
  - More seriously, scaling and adding are the two operations that constrain you to linearity.
- If we consider $a\vec{w} + b\vec{v}$, the linear combination of the two vectors, $\vec{w}$ and $\vec{v}$, on a plane, we have one of three possibilities:
  1. Degenerate case: both were $\vec{0}$, so no amount of scaling gets us away from the origin.
  2. Unfortunate case: both point in the same direction (or in the opposite), so we can only reach points in that line.
      - Trivially, if one vector is $\vec{0}$, then it's effectively parallel to any other, so we're limited to a line.
  3. General case: we can reach any point on the plane.

### Span

#### Definition
- The span of two vectors is the set of all possible vectors that you can reach with their linear combination.
- Another way to look at is: Which vectors you can generate using only the two fundamental operations, vector addition and scalar multiplication?
 - This gets at something deeper: the span of a set of vectors is the smallest vector space containing those vectors.


#### Point representation
- We can think of a vector in terms of the point where its tip is (and assume that the tail is on the origin).
  - The line itself then represents every vector whose tip sits on that line.
  - Likewise, if we consider each of the points on a grid, then these represent each of the vectors from the origin to those points.
- The conclusion 3Blue1Brown draws is that, when considering a single vector, think of it as an arrow, but when considering a set of vectors, it's convenient to think of them as the points (at their tips).

#### Three-dimensional span

- The span of two 3D vectors cuts out a plane that intersects the origin.
  - The set of all possible vectors whose tips sit on that plane is the span of the vectors.
    - This summary is intentionally ignoring the various degenerate and unfortunate cases.


- We can find the span of three 3D vectors, $a\vec{v} + b\vec{w} + c\vec{u}$, and depending on the number of linearly independent vectors, the span cuts out a different space.
  - 0 linearly independent vectors: origin only. This happens when all the vectors are $\vec{0}$.
  - 1 linearly independent vector: line through the origin.
  - 2 linearly independent vectors: plane through the origin.
  - 3 linearly independent vectors: entire 3D space.
- When adding a vector does not increase the span, we say that the vectors are linearly dependent (because linear!).
  - Yet another way to say this is that the useless vector can be expressed as a linear combination of the others, since it's already in their span. In other words, $\vec{u} = a\vec{v} + b\vec{w}$ for some values of $a$ and $b$.
    - The unstated assumption is that the vectors in the existing collection must already be linearly independent for the resulting collection to be independent if the new vector enlarges the span.
  - Otherwise, the span includes every possible 3D vector.
    - One way to look at this is that the third vector moves that plane along another dimension, so that it sweeps the entire space.
    - Another way to look at it is that you have three scalars to let you access all three dimensions.
- The flip side is that, if each vector adds to the span, they're linearly independent. Such originality, much smarts.
  - Symbolically, $\vec{w} \ne a\vec{v}$ for all values of $a$, and $\vec{u} \ne a\vec{v} + b\vec{w}$ for all $a$ and $b$, so long as $\vec{v} \ne\vec{0}$.
  - An even broader definition is $a\vec v+b\vec w+c\vec u=\vec 0$ has only the solution $a=b=c=0$.

## Matrices as linear transformations

### Linear transformation

- Transformation means a function.
  - Why not just call it a function? Well, transformation implies movement, and this can be visualized.
- What makes a transformation linear?
  - Visually, it has two properties:
     1. All lines remain lines, without getting curved.
     2. The origin must remain fixed.
  - Roughly, they keep grid lines parallel and evenly spaced.

## Transformation as new hats

- Transformation can be understood as changing the basis.
- Start by expressing the $\vec{v} = \begin{bmatrix} -1 \\ 2 \end{bmatrix}$ using the equivalent notation of: $$\vec{v} = -1\hat{i} + 2\hat{j}$$.
- Transformation replaces the bases, $\hat{i}$ and $\hat{j}$, with new versions, $\hat{i}'$ and $\hat{j}'$, so that: $$\vec{v}' = -1\hat{i}' + 2\hat{j}'$$.
- By definition, for $\mathbb{R}^2$ (two-dimensional space): $$\hat{i} = \begin{bmatrix} 1 \\ 0\end{bmatrix}, \hat{j} = \begin{bmatrix} 0 \\ 1\end{bmatrix}$$.
  - In a desperate effort to feel cool about themselves while confusing normies, LLM bros call these "one-hot". Then they giggle as they write Python functions with names like `hot_or_not`. Probably.
  - In a desperate effort to feel cool about themselves while confusing normies, Math bros also call these $\hat{e}_1$ and $\hat{e}_2$. Well, actually, that's kind of nice. It scales to arbitrary dimensions without ever running out of letters.
- Let's say that the linear transformation changes them to: $$\hat{i}' = \begin{bmatrix} 1 \\ -2\end{bmatrix}, \hat{j}' = \begin{bmatrix} 3 \\ 0\end{bmatrix}$$.
- Now we can plug the values into the formula to get: $$\vec{v}' = -1\begin{bmatrix} 1 \\ -2\end{bmatrix} + 2\begin{bmatrix} 3 \\ 0\end{bmatrix}$$.
- This is just a linear combination, so we multiply the vector by the scalar and add them together: $$\vec{v}' = \begin{bmatrix} -1(1) + 2(3) \\ -1(-2) + 2(0)\end{bmatrix} = \begin{bmatrix} 5 \\ 2\end{bmatrix}$$.
- If we plug in $\vec{v} = \begin{bmatrix} x \\ y \end{bmatrix}$, we get:  $$\vec{v}' = x\begin{bmatrix} 1 \\ -2\end{bmatrix} + y\begin{bmatrix} 3 \\ 0\end{bmatrix} = \begin{bmatrix} 1x + 3y \\ -2x + 0y\end{bmatrix}$$

## The transformation matrix

- The above shows that the transformation is described in full by four numbers (the coordinates for $\hat{i}'$ and $\hat{j}'$), and we can plug these into a simple formula to give us $\vec{v}'$ for any $\vec{v}$.
- It's common to package $\hat{i}'$ and $\hat{j}'$ into a matrix where each column corresponds to a basis: $$\begin{bmatrix}1 & 3 \\ -2 & 0\end{bmatrix}$$
- Let's generalize so that $\hat{i}' = \begin{bmatrix} a \\ c\end{bmatrix}, \hat{j}' = \begin{bmatrix} b \\ d\end{bmatrix}$, yielding: $$\begin{bmatrix}a & b \\ c & d\end{bmatrix}$$
- So for $\begin{bmatrix} x \\ y\end{bmatrix}$, you get:
$$\vec{v}' = x\begin{bmatrix} a \\ c\end{bmatrix} + y\begin{bmatrix} b \\ d\end{bmatrix} = \begin{bmatrix} ax + by \\ cx + dy\end{bmatrix}$$
- And if you're a bad teacher, you could move the matrix to the left of the vector, and declare that this is matrix multiplication, without offering any explanation. $$\begin{bmatrix}a & b \\ c & d\end{bmatrix}\begin{bmatrix}x \\ y\end{bmatrix} = \begin{bmatrix} ax + by \\ cx + dy\end{bmatrix}$$
  - Mission accomplished, if your mission was to force students to memorize without comprehension.

## Worked examples of transformations

# Rotation

- If we rotate the basis $90^\circ$ counterclockwise: $$\hat{i}' = \begin{bmatrix} 0 \\ 1\end{bmatrix}, \hat{j}' = \begin{bmatrix} -1 \\ 0\end{bmatrix}$$
- We get this matrix: $$\begin{bmatrix}\hat{i}' & \hat{j}'\end{bmatrix} = \begin{bmatrix}0 & -1 \\ 1 & 0\end{bmatrix}$$
- We could then figure out the transformed vector by multiplying: $$\vec{r}' = \begin{bmatrix}0 & -1 \\ 1 & 0\end{bmatrix} \begin{bmatrix}x \\ y\end{bmatrix}$$

# Shear

- The $\hat{i}'$ remains fixed but $\hat{j}'$ changes: $$\hat{i}' = \begin{bmatrix} 1 \\ 0\end{bmatrix}, \hat{j}' = \begin{bmatrix} -1 \\ 1\end{bmatrix}$$
- We get this matrix: $$\begin{bmatrix}\hat{i}' & \hat{j}'\end{bmatrix} = \begin{bmatrix}1 & 1 \\ 0 & 1\end{bmatrix}$$
- We could then figure out the transformed vector by multiplying: $$\vec{r}' = \begin{bmatrix}1 & 1 \\ 0 & 1\end{bmatrix} \begin{bmatrix}x \\ y\end{bmatrix}$$

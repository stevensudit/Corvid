# What is CorvidSim?

CorvidSim, which is located in the "Sim" subdirectory, is a project leveraging and exercising the Corvid library. Using the WebSocket and ECS code, it hosts an eponymous browser-based tower defense game.

# Steps

## Static HTML
- corvid_sim.cpp -- Launches an HTTP server on localhost that serves files in "web" subdirectory.
- web/index.html -- Simple page that's served up.
---
---
---
## WebSocket POC
- Serve JS/TS by including the script in "index.html".
- Using this to connect to server using WebSocket and exchange JSON messages (high-level ping/pong).

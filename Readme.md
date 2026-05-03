# Home Furnishing Competition — Multi-Process Simulation

**Course:** ENCS4330 — Real-Time Applications & Embedded Systems
**Instructor:** Dr. Ahmad Afaneh
**Semester:** 2nd Semester 2025/2026
**Due Date:** May 3, 2026

## Team Members

| Name | ID |
|------|----|
| Hamed Musleh | 1221036 |
| Bara Mohsen | 1220829 |
| Ahmad Zuhd | 1222332 |
| Ameed Iyad | 1232942 |

---

## Overview

This project simulates a competitive scenario between two teams responsible for transporting furniture pieces through a chain of members. Each team operates as a pipeline of processes, where coordination, communication, and timing directly impact performance.

The system emphasizes inter-process communication, synchronization, and efficient handling of forward and backward message flows.

---

## Technologies Used

- **Inter-Process Communication (IPC):**
  - Pipes (forward and backward channels)
  - Signals (`SIGUSR1`, `SIGUSR2`, `SIGTERM`)
- **Multi-processing:**
  - `fork()` for process creation
- **Synchronization:**
  - `select()` for managing I/O priorities
- **Visualization (Optional):**
  - OpenGL / GLUT

---

## Build and Execution

### Build the Project

\`\`\`bash
make clean && make
\`\`\`

### Run the Simulation

\`\`\`bash
./furnishing config.txt
\`\`\`

### Run with Visualizer (Optional)

\`\`\`bash
# Terminal 1
./visualizer

# Terminal 2
./furnishing config.txt
\`\`\`

---

## Configuration File (\`config.txt\`)

The simulation behavior is controlled via an external configuration file:

- \`NUM_MEMBERS\` — Number of team members (minimum: 2)
- \`NUM_PIECES\` — Number of furniture pieces (minimum: 1)
- \`MIN_DELAY\` / \`MAX_DELAY\` — Range of random delays
- \`FATIGUE_FACTOR\` — Incremental fatigue per processed piece
- \`WINS_TO_WIN\` — Number of rounds required to win

---

## System Design

### Forward Path (Source → Sink)

Furniture pieces move forward through a pipeline of processes:

\`\`\`
Source → Member1 → Member2 → ... → Sink
        pipe[0]    pipe[1]        pipe[N-2]
\`\`\`

### Backward Path (Acknowledgment / Rejection)

Responses propagate backward through a separate channel:

\`\`\`
Sink → Member(N-2) → ... → Member1 → Source
       bwd[N-2]                       bwd[0]
\`\`\`

---

## Synchronization Mechanism

- \`SIGUSR1\` — Indicates that Team A wins a round
- \`SIGUSR2\` — Indicates that Team B wins a round
- \`SIGTERM\` — Terminates the current round or the simulation

---

## Processing Model — One Piece at a Time

- The Source sends only one furniture piece at a time
- It waits for either \`ACCEPT\` or \`REJECT\`
- Only after receiving a response does it proceed to the next piece

This ensures controlled flow and prevents congestion or race conditions.

---

## OpenMP Usage

OpenMP was experimentally added to parallelize the deck shuffle operation. 
However, since this simulation is **I/O-bound** (processes spend most time 
waiting on pipes and signals) rather than **CPU-bound**, OpenMP provided 
**no measurable speedup** for typical input sizes.

The directives remain in `utils.c` for demonstration purposes:

```c
#pragma omp parallel for if(n > 100)
```

The conditional clause `if(n > 100)` ensures OpenMP only activates when 
the workload is large enough to overcome thread-creation overhead.

## Key Features

- Deadlock prevention using \`select()\` with blocking reads
- Priority handling for backward communication
- Dynamic fatigue model that increases over time
- Avoids immediate reselection of recently rejected pieces
- Fully configurable via external file (no hardcoded parameters)
- Optional OpenGL-based visualization
- Colored logging for easier debugging and tracing

---

## Environment Requirements

- \`gcc -Wall -Wextra -g -O0\`
- Linux (Ubuntu 20.04+ recommended) or WSL2
- Optional: \`sudo apt install freeglut3-dev\`

---

## Notes

If you encounter Windows line endings (\`^M\`) in the \`Makefile\`:

\`\`\`bash
sed -i 's/\r//' Makefile
\`\`\`

or:

\`\`\`bash
dos2unix Makefile
\`\`\`

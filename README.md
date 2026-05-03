```markdown
# 🏠 Multi-Process Furnishing Competition Simulation

## 📌 Overview
This project implements a multi-process simulation in C that models a competitive furniture transportation system between two teams.

Each team operates as a pipeline of processes, where furniture pieces are transferred from a source to a sink through multiple members. The system demonstrates advanced concepts in inter-process communication (IPC), synchronization, and real-time system behavior.

---

## 🎯 Objectives
- Simulate real-time process coordination in a distributed pipeline
- Apply IPC mechanisms in a practical scenario
- Ensure synchronization and avoid race conditions
- Analyze performance under different configurations

---

## 🚀 Features
- Multi-process architecture using `fork()`
- Bidirectional communication:
  - Forward path using pipes
  - Backward path for acknowledgments and rejections
- Signal-based synchronization (`SIGUSR1`, `SIGUSR2`, `SIGTERM`)
- Deadlock prevention using `select()`
- Dynamic fatigue model affecting processing delays
- Configurable simulation via external file
- Optional real-time visualization using OpenGL
- Colored logging for debugging and tracing

---

## 🧠 System Architecture

### Forward Communication (Source → Sink)
```

Source → Member1 → Member2 → ... → Sink
pipe[0]    pipe[1]        pipe[N-2]

```

### Backward Communication (Sink → Source)
```

Sink → Member(N-2) → ... → Member1 → Source
bwd[N-2]                       bwd[0]

```

- Pieces are processed sequentially
- Each piece is either:
  - ✅ Accepted → placed successfully
  - ❌ Rejected → returned to source for retry

---

## ⚙️ Configuration

The simulation is controlled via `config.txt`.

### Example:
```

NUM_MEMBERS=3
NUM_PIECES=1000
MIN_DELAY=10
MAX_DELAY=50
FATIGUE_FACTOR=10
WINS_TO_WIN=3

````

### Parameters:
- `NUM_MEMBERS` → number of processes per team
- `NUM_PIECES` → number of furniture pieces
- `MIN_DELAY` / `MAX_DELAY` → processing delay range
- `FATIGUE_FACTOR` → delay increase over time
- `WINS_TO_WIN` → rounds required to win

---

## 🛠️ Technologies Used
- C Programming (System-Level)
- Linux System Programming
- Inter-Process Communication (IPC):
  - Pipes
  - Signals
  - FIFOs
- Synchronization:
  - `select()` system call
- Parallelism (optional): OpenMP
- Visualization: OpenGL / GLUT

---

## ▶️ Build & Run

### Build
```bash
make clean && make
````

### Run Simulation

```bash
./furnishing config.txt
```

### Run with Visualizer (Optional)

```bash
# Terminal 1
./visualizer

# Terminal 2
./furnishing config.txt
```

---

## ⚡ Processing Model

* The source sends one piece at a time
* Waits for:

  * `ACCEPT` → continue
  * `REJECT` → retry later
* Ensures:

  * Controlled execution
  * No congestion
  * No race conditions

---

## 🧪 Performance Note

OpenMP was used to parallelize the shuffle operation:

```c
#pragma omp parallel for if(n > 100)
```

However, the system is I/O-bound, so OpenMP does not provide significant speed improvement.

---

## 🏆 Key Concepts Demonstrated

* Process creation and management (`fork`)
* IPC (pipes, signals, FIFOs)
* Synchronization and deadlock avoidance
* Real-time system modeling
* Event-driven design using `select()`

---

## 💻 Requirements

* GCC (`gcc -Wall -Wextra -g -O0`)
* Linux / WSL2
* Optional:

```bash
sudo apt install freeglut3-dev
```

---

## ⚠️ Notes

If you encounter Windows line endings (`^M`):

```bash
sed -i 's/\r//' Makefile
```

or:

```bash
dos2unix Makefile
```

---

## 👨‍💻 Authors

* Ahmad Zuhd
* Hamed Musleh
* Bara Mohsen
* Ameed Iyad

Birzeit University — Computer Engineering
Course: ENCS4330 (Real-Time Applications & Embedded Systems)

```

---

If you want next step 👉 I can make:
- 🔥 LinkedIn post for this project  
- 💼 CV description (very important for internships)
```

# Besa-Archive
A high-fidelity C++ adversary simulation tool designed to emulate the multi-process "swarm" architecture and self-healing watchdog patterns characteristic of TA505. This project demonstrates advanced NTFS ownership manipulation, sparse encryption techniques, and process resilience for testing EDR/XDR behavioral detection and response capabilities in a controlled research environment.

## 🛠 Technical Architecture
The core of this simulation is the Distributed Swarm Model. Rather than a single process encrypting files, the DLL orchestrates a mesh of workers that monitor and protect each other.
 **1. Process Orchestration & Swarm Spawning**
- The DLL uses a recursive spawning logic via rundll32.exe.
- The Orchestrator (ID 0): Scans the target directory, identifies subdirectories, and launches a dedicated "Worker Agent" for each one.
- The Worker Agents: Each agent is responsible for a specific folder tree. This bypasses many "single-process threshold" detections by spreading the I/O load across dozens of legitimate Windows processes.

 **2. Self-Healing Watchdog (Mutex Mesh)**
To simulate high resilience, the script implements a "Peer Watchdog":
- Each agent creates a unique global mutex: `Global\SvcHealth_[ID]`.
- Each agent simultaneously runs a background thread that monitors its neighbor (e.g., Agent 1 monitors Agent 2, Agent 2 monitors Agent 3).
- If a peer's mutex disappears (due to process termination), the watchdog immediately respawns the peer via CreateProcessW.

 **3. Permission & Ownership Bypass**
The simulation includes logic to bypass standard Access Control Lists (ACLs):
- **Privilege Escalation**: Attempts to enable `SE_TAKE_OWNERSHIP_NAME` and `SE_BACKUP_NAME` on the process token.
- **Forceful Ownership**: Uses `SetNamedSecurityInfoW` to seize ownership of files, followed by `SetEntriesInAclW` to grant `GENERIC_ALL` permissions, ensuring encryption can proceed even on restricted files.

 **4. Sparse Encryption Engine**
To maximize speed and evade heuristic detection of heavy disk writes:
- **Algorithm**: Rolling XOR using a 16-byte key (0xDE AD BE EF...).
- **Sparse Mode**: Only the first 16KB of each file is modified. This renders most file types (headers/metadata) unreadable while significantly reducing the I/O footprint.
- **Extension Targeting**: Only processes high-value data (e.g., .pdf, .docx, .sql, .pem, .zip).

## 🚀 Execution Guide
**Compilation**
The project requires linking against advapi32.lib, shell32.lib, and user32.lib. Ensure you compile as a DLL (x64 recommended).

**Usage Syntax**
The DLL is triggered via the RunArchive export:

**Initiate Orchestrator:**
```
rundll32.exe FileUtility.dll,RunArchive 0 "C:\Users\"
```

**Safety Features (The "Kill-Switch")**
For safety during red team engagements, the script constantly checks for a termination signal:
- **Stop File**: Create an empty file at C:\Users\Public\stop.txt.
- **Effect**: All agents and watchdogs will detect this file and exit gracefully within 1–3 seconds.

Mutex Activity: A sudden spike in Global\SvcHealth_ mutex creation across multiple processes.

Log Activity: Audit logs are written to C:\Users\Public\deployment_audit.log for post-simulation review.

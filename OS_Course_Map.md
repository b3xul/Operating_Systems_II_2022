# OS Services
## Services Provided
- User interface
- Program execution
- IO operations
- File system manipulation
- Communications
- Error detection
- Resource allocation
- Logging (accounting)
- Protection and security
## System calls
- Process control
- File management
- Device management
- Information maintenance
- Communication
- Protection
## OS Design strategies
- Monolithic vs Microkernel vs Hybrid
- Layered vs non-layered
## Booting process
1. Write OS source code in C+assembly
2. Configure the OS for the system on which it will run ()
3. Compile the OS
4. Compile kernel modules
5. Install the OS
6. Boot the PC and its new OS
	1. Execution starts at predefined memory location on ROM: boot block
	2. It initializes disk controller, takes from disk the bootstrap loader = BootLoader(Windows Boot Manager/GRUB) and execute it
	3. BootLoader goes on disk again and starts executing the first instruction of the OS that we installed/the only one installed
## Monitoring tools
---
# Memory Management
## Functions
### Protect address space from other processes
- Base register + Limit Register: They must be updated (in kernel mode) at each context switch with the info contained in the PCB. They must be checked at each memory access by hw, sending a trap in case of invalid access. 1 location associated to each process! No relocatable
- Address Binding source code symbolic addresses related to the single .c module
	- compile+linking time: addresses related to the single .c file -> addresses related to the global .exe program: logical addresses (or virtual addresses if those could refer to information that should be on ram but are actually on disk)
		- Static linking: linking at compile time
		- Dynamic linking(Linker-assisted Dynamic loading): linking at run time. User program marks modules as dynamic: typically used for shared libraries. When dynamic function is called at first a stub (plt.printf) will be present in the link table entry, used to locate and load that module. Then the module is loaded and the link table will contain the address instead of the stub.
	- load time: absolute addresses bound with the base address associated with the process: physical addresses
	- execution time: absolute addresses bound with the base address associated with the process, that can be moved at runtime: physical addresses
		- Dynamic loading: used by embedded system, user program decides which sections of the program to load and when. We need the OS support to invoke loader and to leave symbols unresolved until runtime.
- MMU translate logical into physical addresses. In simple systems logical=physical. In others, the value of the relocation (base) register (in the MMU) for that process is added to each logical address to generate the physical address. The content of the relocation register can change at runtime.
## Contiguous Memory Allocation
- Keep information about
	- Allocated partitions (PCB processes in execution)
	- Free partitions list of all partitions and their sizes
- Variable partitions allocation strategies
	- First fit (first hole)
	- Best fit (smallest hole)
	- Worst fit (largest hole)
- Fragmentation
	- External: scattered contiguous space outside partitions -> can be reduced by compaction (defragmentation). Processes must be relocated at runtime, updating PCBs. Keep into consideration DMA, avoiding moving processes in IO queue
	- Internal: increase next allocations speed by giving to partitions small additional memory to avoid keeping small holes
## Paging
- Logical Pages and Physical Frames.
- First part of address is used as index on the page table. Second part of address is the offset inside the physical frame indicated by the entry on the page table. Per risparmiare spazio invece di usare l'intero indirizzo si puo' usare anche solo l'indice della pagina/frame.
- Fragmentation
	- External: 0
	- Internal: max FRAME_SIZE-1 bytes for each process (on average half frame). Can be reduced by making frames smaller, but then the page table will be bigger since they will index more frames.
- Page Table
	- Entry
		- Frame number
		- Caching disabled
		- Referenced
		- Modified (dirty)
		- Protection
		- Present/absent
		- Valid/invalid: avoid keeping all process frames in the page table
	- Implementations
		- Stored on hw registers. Faster access, slower context switch, too big for registers today
		- Page-table base register points to the page table kept in memory. Page-table length register indicates its size. Slower access, faster context switch, can fit all frames
	- Structure
		- Per-process page table. Each process thinks that it has the whole physical address space for itself. Easy to share pages, just make page tables point to the same frame.
			- Hierarchical (2-3 level paging). Same total space for the page table, but can be allocated non-continuously. Does not scale: to address 64 bit address spae you would need 16 GB for outer page table + 4kb for inner page table. Requires many tables accesses for each memory access.
			- Hashed (and chained). Each entry contains the page number, the frame number and the pointer to the next element of the list corresponding to that entry in the hash table. We can search a tradeoff between hash table size and lists length.
		- System page table: Inverted. Each entry in the page table refers to a single physical frame. Sharing pages becomes more difficult, chaining techniques must be used to map more than one logical page to the same physical frame.
			- Ordered by physical frame. When a pid/asid : logical page arrives, we need to search the whole table to find where that entry is -> which is the corresponding physical frame. 
			- Hashed. pid:page is hashed, and the corresponding frame number is found on the entry
- Translation Lookaside Buffer associative cache memory stored on the processor itself(60-1024 entries): caches most used page table entries. Retrieves in O(1) frame number given page number. Could have different levels of cache.
	- Entry
		- Frame number
		- Valid/invalid
		- Modified
		- Protection
		- Address Space IDentifiers to identify the process that owns that frame. Without it, at each context-switch the TLB would need to be invalidated.
	- Replacement policies
		- LRU
		- Round Robin
		- Random
		- Some entries can be wired down
	- `Effective Access Time=%hit*time hit + %miss*time miss`
## Swapping
- Remove process from memory into the backing store (swap partition:faster, limited size/swap file: slower, infinite size) and vice versa
## Real architectures example
---
# Virtual Memory
## Ideas
- Increase CPU utilization, occupy less memory with unused instructions, less IO to load or swap programs, allow big programs to be executed even if only partially in memory.
- Indirizzo logico e' sicuramente in RAM, indirizzo virtuale potrebbe far riferimento a dati al momento sul backing store.
- Swap a livello di pagina invece che a livello di processo.
- Virtual address space visto da un processo: Code segment, Data segment, heap->, <-stack 
- Easy to share libraries
## Demand paging
- Bring pages into memory only when needed to reduce total memory needed. Valid address IO request -> if page not in memory, bring it to memory
- To work effectively it needs:
	- Page table with valid/invali bit
	- Locality of reference
	- Backing store
	- Instruction restart mechanism
- Strategies:
	- Bring entire process in memory at load time -> faster execution, more memory used
	- Bring one portion of the process in memory at load time
	- Pure demand paging: Only bring pages of the process in memory when requested -> slower execution, less memory used
- When a page is requested, if it contains the (Invalid bit), during the address translation phase
	1. the MMU sends a Page Fault interrupt, so the OS will determine
	2. save user registers and process state
	3a. if the reference is invalid: abort
	3b. if it is just not in memory, start to execute the proper routine
	4a. If memory ends (empty free-frames list), we also need to decide which frames to swap out to make room for the new one.
		Then write the victim frame to disk if it was dirty
	4b. Issue read from disk to free frame:swap in the requested page in one of the free frames from the free frame list, zero-filling it on demand.
	5. While waiting read, allocate CPU to another user, until interrupt is received, register and state of other user are saved.
	6. Set physical frame and valid bit in page table
	7. Wait for cpu to be reallocated to this process
	8. Restore user register and process state and reart instruction that caused the page fault.
- Effective Access Time = 1-page fault rate * memory access + page fault rate* (page fault overhead+swap page out+swap page in)
	- Page fault overhead = service interrupt + restart process (1-100 microseconds)
	- Swap page out+swap in = 8 ms (HDD average) (* 2 if frame was dirty)
	- 1 page fault every 400 000 memory accesses to avoid degrading performances of more than 10%
- Optimizations:
	- Swap partition optimized to reduce swap time
	- Swap out to swap space instead of disk
	- Discard non-modified/non-modifiable pages instead of swapping out
	- Discard read-only pages first
	- Copy-on-write: pages shared between parent and child processes are not copied until one modifies them. Only copy (use) pages actually modified
	- vfork: share same pages between parent and child. Don't even copy pages when child wants to modify them. Typically won't modify anything since calling exec generates a new address space.
## Page replacement algorithms
- Minimize the average page fault rate for a benchmark of memory accesses strings (programs)
- f(A,m) = sum over all w reference string of p(w) * Faults(a,m,w) / len (w) -> probabilita' empirica di page fault dato un algoritmo e un numero di page frames a disposizione dei programmi.
- FIFO
	- Belady's Anomaly
- LRU: remove page that has not been accessed the longest.
	- Exact Implementations too big overhead:
		- Software solutions: unfeasible 1 interrupt for each memory access!
		- Counter: associate counter to each page, containing the timestamp (clock) of the last reference -> O(n) to oldest page to remove
		- Stack implemented using double-linked list, having most recent access on head: O(1). Each new access in memory must update 6 pointers. 1 memory access->6 memory accesses
	- Approximated implementation
		- Reference bit: remove pages with bit=0. Page reference set bit to 1.
		- Second-chance: FIFO+reference bit. Pointer to oldest element so that best access is O(1), but only replace it if its reference bit=0, otherways put it at 0 and keep looking at oldest elements until one suitable for removal is found. Statistically < O(n).
		- Enhanced second-chance. FIFO + reference bit + modify bit. 0,0 -> 0,1 -> 1,0 -> 1,1. Best case O(1), Worse case O(4n). Statistically in the middle, but better performance regarding future accesses.
- Least Frequently Used -> high overhead, low performances
- Most Frequently Used -> high overhead, low performances
- Optimal algorithm: must know the future
## Other Optimizations that can always be applied:
- Keep always pool of freee frames, so that swap outs will be done when system is idle instead of when accessing page
- Keep list of modified pages to write out when backing store idle
- Keep free frame content intact so that it can be used again if the content is referenced again, so that you don't have to swap it in again. Minor page fault which would result in less performance loss.
## Frame allocation Strategies
- How many frames to give to each process
- Which frames to replace
- Minimum number of frames needed by a process is defined by the instructions supported by that computer architecture
- Fixed allocation of all available frames
	- Equal
	- Proportional to the size of the process (may change with time)
- Global replacement (one process can take frames from others) vs Local replacement (only select one of its frames)
- Page reclaiming triggered under a certain threshold to avoid reaching 0 free frames available.
## Working Set, Page Fault Frequency and Thrashing
- Process doesn't have enough frames -> high page fault rate -> low cpu utilization -> os tries to increase multiprogramming because many processes are just waiting for disk responses -> even less frames available -> system freezes. Thrashing occurs when the sum of the size of all localities is greater than the total memory size
- Working Set of a process= total number of page references occurred in the most recent delta (time unit/number of instructions)
	- To avoid thrashing we want to give to each process all the pages required to satisfy their WS, and reclaim page if thrashing condition occurs
	- Estimating a delta of the right size to contain exactly one locality is hard. 
	- To track the working set of a process I can associate to each entry in its page table n bits (the higher, the more accurate, the higher cost) to keep the history of references to that page, updated every x time units. 
- Monitor number of page faults (not all page references!). If the time since the previous fault is too low (frequency too high), add frames to the resident set of the faulting process, else remove from its reference set all pages with reference bit=0 
## Allocating kernel memory
- Buddy system (power of 2 allocator) -> high external fragmentation
- Slab Allocator:
	- Slabs are physically contiguous pages in memory.
	- Each cache will contain n entries of a certain category of kernel objects. They consists of 1 or more slabs.
	- When cache is full a new cache is allocated.
	- 0 internal fragmentation. Preallocation of kernel object for fast performances. Flexibility in memory usage
	- Slob uses only 3 categories to avoid wasting memory in preallocation
---
# File System
## HDD
- HW
	- Disk head
	- Platter (single disk)
	- Cylinder: same track on all platters
	- Track: circle on one platter
	- Sector: portion of track
- I/O time composition
	- Transfer rate: data flow speed (6Gbps theoretical, 1Gbps effective)
		- transfer time depends on amount of data: size/transfer rate
	- Random-access time=positioning time=access time
		- Seek time: move disk head to cylinder (3-12ms)
		- Rotational latency: wait for sector to rotate under disk head (2-5.56ms). Time for 1 rotation: 7200 RPM -> 1/(7200/60)=1/120RPS=8.33ms. On average 4.17ms.
	- Controller overhead
- HDD Disk Scheduling Algorithms: schedule handling of requests in IO queue to minimize seek time. When disk is idle requests are served immediately.
	- First Come First Served
	- SCAN: `---> <--- --->` : Tempo di attesa non uniforme
	- C-SCAN: `---> ---> --->`: Tempo di attesa uniforme
## NVM (SSD, USB, Flash)
- Properties
	- Read/Write in pages (4kb/16kb)
	- Erasure in blocks (MB)
- NAND Flash Controller Firmware
	- Flash Translation Layer
		- Tracks valid logical blocks
		- Translates OS commands designed for HDDs to SSDs
	- Implement garbage collector to free invalid pages
	- Overprovisioning provide working space for Garbage Collectror
	- Wear Leveling algorithms increase lifespan
- SSD Scheduling Algorithms: Combine write requests on the same page while they are still in the queue to minimize the number of block erasure, limiting wearing
## Properties
- Disk Attachment
	- SATA
	- NVMe interface connects directly to PCI bus
	- USB
- Address mapping
	- OS:Indirizzo logico->Controllore disco:Pagina SSD/Traccia+settore HDD
- Error Handling
	- Detection
		- Checksum
		- Parity bit
		- Cyclic Redundancy Check
	- Correction
		- Error Correction Code
		- Hamming Code
- Formatting
	- Low-level: done by manufacturer: divide disk into sectors/pages
	- High-level: done by the OS
		- Disk partitioning (MBR contains both boot code and partition table)
		- Volume creation: File system installation
## Network Attached Storage and Redundant Array of Inexpensive Disks
- Increase data reliability while wasting as little space as possible
- Managed by OS module or external dedicated board.
- With 1 disk:
	- Mean time to data loss = Mean Time to Failure disk1
- With 2 clones of the same disk:
	- Fault probability of 1 disk in each istant = 1/MTTF disk
	- MTTF system = MTTF disk/2 
	- Fault probability system = 2/MTTF
	- Fault probability during repair = Fault Probability of 1 disk in each istant * MTT Repair = MTTR/MTTF
	- MTTF During Repair = MTTF/MTTR
	- Mean Time to Data Loss =  1/ (Fault Probability System * Fault Probability During Repair) = 1/ ( (2/MTTF disk) * MTTR/MTTF ) = MTTF^2/2MTTR
- RAID Levels
	- 0: non redundant striping: data scattered across disks to allow for parallel reads. + performance, - reliability
	- 1-3: - performance, + reliability, faster recovery, big space wasted using disk clones
	- 4-6: - performance, + reliability, slower recover, less space wasted using error correction codes instead of disk clones
## IO
- IO HW
	- Ports
	- Bus
	- Device Controller
		- Registers (command, status, data) exposed to the OS through direct IO instructions ( D IN, D OUT) or Memory mapping on physical addresses. Controller collegato al segnale proveniente da istruzione o da bus indirizzi quando passa indirizzo associato
	- Direct Memory Access controller
		1. OS module Device Driver writes information on DMA command register mapped in memory
			-  Source and destination addresses
			- Read or write
			- Numer of bytes
		2. DMA will occasionally steal cycles of CPU for the data bus while transferring from memory to memory to avoid double transfer
		3. Sends interrupt when operation completed
	- Types of devices
		- Character vs Block
		- Sequential vs Random access
		- Synchronous vs Asynchronous (not related with how the OS handled the operation) 
- Application IO interface
	- 1 module for each device category
- Kernel IO subsystem
	- Scheduler IO operations: queues to/from devices
	- Buffering mechanisms:
		1. Copy semantics: To avoid to the device to access user data directly, so that user cannot generate an undefined behavior on the device by modifying data while it is working on them (asynchronous devices). We copy the data in a kernel buffer and the device will work on our safe copy.
		2. Deal with trasfer size mismatch by using a bigger buffer
		3. Deal with device speed mismatch (network card vs ssd): Double buffering
	- Caching copy of data
	- Spooling: hold output from slow device (printers)
	- Device reservation: exclusive access to device
	- Error handling retrying operations, logging problems
	- IO protection -> only kernel can access devices directly, users need to use syscalls
	- Data structures
		- Open file tables
		- Network connections
		- State information about IO components and devices
		- Buffers
		- Track Memory allocation, dirty blocks
	- Power management 
- IO requests to HW operations
	1.  Programma utente chiama syscall read o write 
	2.  Sistema operativo esegue syscall in kernel mode 
	3.  -> layer del file system si infila qui <- 
	4.  Sono già in grado di soddisfare richiesta I/O? Il dato è già presente in un buffer del kernel? (Caching)
	    -   Sì: Ritorno dato in user space tramite valore di ritorno della syscall. 
	    -   No: Continuo 
	5.  Inviare la richiesta I/O al device driver che la dovrà gestire e, se il device è bloccante, blocca il processo: dire allo scheduler di spostare il processo che ha effettuato la chiamata da Running a Waiting 
	6.  Device Driver conosce la categoria di hw a cui quel device appartiene e quindi traduce la richiesta usando le funzioni di interfaccia appropriate per comunicare con quel dispositivo 
	7.  Device Controller conosce il dispositivo specifico e traduce le funzioni di interfaccia nelle funzioni specifiche per quell'hardware, soddisfa la richiesta e genera un interrupt. OPPURE, se il dispositivo non è bloccante non soddisfa ancora la richiesta, genera solo un interrupt per dire al device driver che la richiesta è stata presa in carico e verrà soddisfatta in futuro. A quel punto il device driver ritorna tramite la syscall questa informazione, e il processo chiamante può continuare la sua esecuzione finchè non verrà mandato un altro interrupt quando l'operazione sarà conclusa 
	8.  CPU riceve interrupt del dispositivo, esegue Interrupt Handler-> chiama funzione di interfaccia del device driver corretto per riferire che il dato è pronto / l'operazione si è conclusa. 
	9.  Device Driver legge il dato ricevuto e lo manda al kernel 
	10.  Kernel lo ritorna come valore di ritorno della syscall.
# File system interface and implementation
- File concept
	- Boot control block
	- Volume control block
	- Directory structure
		- List
		- Hashtable
	- File control block=inode
	- Open file table system wide:
		- disk location of the file
		- access rights
		- count of processes
		- shared reader lock
		- exclusive writer lock
	- open file table per process:
		- offset in the file
		- mode of operation r/w
	- structure
		- byte
		- line
		- complex structure (db) 
	- file allocation strategies
		- contiguous
		- linked
		- File Allocation Table
		- hierarchical indexed (inodes)
- Access methods
	- sequential
	- direct
	- indexed through additional index file: key-block addresss in the file
- Disk and directory structure
	- Goals
		- Efficiency
		- Convenient naming
		- Grouping of files
	- Types
		- Single level
		- 2-level
		- tree
		- DAG
		- Graph
- Free space management
	- list
	- bitmap
- File system Layers
	- Logical file system: device directory, inodes (File Control Blocks)
	- File organization module: files, logical address -> physical block
	- Basic file system: Logic blocks, buffers, caches
	- Device drivers: Map logic blocks to physical addresses

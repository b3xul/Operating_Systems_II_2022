/*
The write is one of the most basic routines provided by a Unix-like operating system kernel. It writes data from a buffer declared by the user to a given device, such as a file. This is the primary way to output data from a program by directly using a system call. The destination is identified by a numeric code. The data to be written, for instance a piece of text, is defined by a pointer and a size, given in number of bytes.

write thus takes three arguments:

The file code (file descriptor or fd).
The pointer to a buffer where the data is stored (buf).
The number of bytes to write from the buffer (nbytes).

It returns -1 if an error occurs

*/

#include <types.h>		 // userptr_t, size_t
#include <kern/unistd.h> // STDOUT_FILENO, STDERR_FILENO
#include <lib.h>		 // kprintf, putch
#include <kern/syscall.h>
#include <syscall.h>

#if OPT_FILE_SYSTEM
#include <limits.h>		// OPEN_MAX
#include <vfs.h>		//vfs_open
#include <kern/errno.h> //ENOENT
#include <current.h>	//curproc
#include <proc.h>

#include <uio.h>
#include <kern/iovec.h>
#include <vnode.h>
#include <copyinout.h>

/* max num of system wide open files */
#define SYSTEM_OPEN_MAX (10 * OPEN_MAX)

struct openfile
{
	struct vnode *vn;
	off_t offset;
	unsigned int refCount;
	// int lock; possiamo aggiungere accesso in mutua esclusione in futuro
};

struct openfile systemFileTable[SYSTEM_OPEN_MAX];
/*

void openfileIncrRefCount(struct openfile *of)
{
	if (of != NULL)
		of->countRef++;
}

*/

int sys_open(userptr_t pathname, int flags, mode_t mode, int *errp)
{
	/* 1. Crea nuovo openfile */
	struct openfile *of = NULL;
	struct vnode *vn;
	int result = 0;
	// int vfs_open(char *path, int openflags, mode_t mode, struct vnode **ret)
	result = vfs_open((char *)pathname, flags, mode, &vn);
	if (result)
	{
		*errp = ENOENT;
		vfs_close(vn);
		return -1;
	}
	/* 2. Inserisco struct openfile dentro systemFileTable (cercando prima posizione vuota nella tabella) */
	for (int i = 0; i < SYSTEM_OPEN_MAX; i++)
	{
		if (systemFileTable[i].vn == NULL)
		{
			of = &systemFileTable[i];
			of->vn = vn;
			of->offset = 0; // TODO: handle offset with append (offset != 0)
			of->refCount = 1;
			break;
		}
	}
	if (of == NULL)
	{
		// no free slot in system open file table
		*errp = ENFILE;
	}
	else
	{
		/* 3. Inserisco puntatore alla struct openfile dentro p_fileTable */
		for (int fd = STDERR_FILENO + 1; fd < OPEN_MAX; fd++)
		{
			if (curproc->p_fileTable[fd] == NULL)
			{
				curproc->p_fileTable[fd] = of;
				return fd;
			}
		}
		// no free slot in process open file table
		*errp = EMFILE;
	}
	vfs_close(vn);
	return -1;
}

int sys_close(int fd)
{
	if (fd < 0 || fd > OPEN_MAX)
		return -1;
	/* 1. Ottengo openfile da processFileTable  */
	struct openfile *of = curproc->p_fileTable[fd];
	if (of == NULL)
		return -1;

	/* 2. Nullo openfile* da processFileTable */
	curproc->p_fileTable[fd] = NULL;
	/* 3. Diminuisco countref  */
	of->refCount--;
	if (of->refCount > 0)
		return 0;
	/* 4. Se countref arriva a 0, elimino openfile in systemFileTable (systemFileTable[i].vn = NULL)) */
	struct vnode *vn = of->vn;
	if (vn == NULL)
		return -1;
	of->vn = NULL; // equivalent to (systemFileTable[i].vn = NULL) since we have the pointer to that openfile

	/* 5. Chiudo vnode con vfs_close  */
	vfs_close(vn);
	return 0;
}

static long file_write(int fd, userptr_t buf, size_t count)
{
	if (fd < 0 || fd > OPEN_MAX)
		return -1;
	/* 1. Ottengo openfile da processFileTable  */
	struct openfile *of = curproc->p_fileTable[fd];
	if (of == NULL)
		return -1;
	struct vnode *vn = of->vn;
	if (vn == NULL)
		return -1;

	struct uio ku;
	struct iovec iov;

	// Se usiamo memoria kernel possiamo sfruttare uio_kinit, altrimenti stessa strategia di prima
	void *kbuf = kmalloc(count);
	copyin(buf, kbuf, count); // copy semantics!

	uio_kinit(&iov, &ku, kbuf, count, of->offset, UIO_WRITE);
	int result = VOP_WRITE(vn, &ku);
	if (result)
	{
		return result;
	}
	kfree(kbuf);
	of->offset = ku.uio_offset;
	return count - ku.uio_resid;
}
static long file_read(int fd, userptr_t buf, size_t count)
{
	if (fd < 0 || fd > OPEN_MAX)
		return -1;
	/* 1. Ottengo openfile da processFileTable  */
	struct openfile *of = curproc->p_fileTable[fd];
	if (of == NULL)
		return -1;
	/* 2. Ricavo vnode riferito da quell'openfile */
	struct vnode *vn = of->vn;
	if (vn == NULL)
		return -1;

	/* 3. Preparo uio struct adeguata per la lettura che voglio fare */
	struct uio u;

	/*
	struct uio {
	struct iovec     *uio_iov;	Data blocks
	unsigned uio_iovcnt;		 Number of iovecs
	off_t uio_offset;			 Desired offset into object
	size_t uio_resid;			 Remaining amt of data to xfer
	enum uio_seg uio_segflg;	 What kind of pointer we have
	enum uio_rw uio_rw;			 Whether op is a read or write
	struct addrspace *uio_space; Address space for user pointer
	};
	*/
	struct iovec iov;
	/*
	struct iovec{
	void *iov_base;  user-supplied pointer
	size_t iov_len;  Length of data
	}
	*/
	iov.iov_ubase = buf;
	iov.iov_len = count; // length of the memory space

	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_offset = of->offset;
	u.uio_resid = count; // amount to read from the file
	/*
	enum uio_seg {
		UIO_USERISPACE,		User process code.
		UIO_USERSPACE,		User process data.
		UIO_SYSSPACE,		Kernel.
	};
	*/
	u.uio_segflg = UIO_USERISPACE;
	u.uio_rw = UIO_READ;

	struct addrspace *as;
	as = curproc->p_addrspace; // Passes directly the user buffer!
	u.uio_space = as;

	/* 4. Leggo da vn all'address space del processo */
	int result = VOP_READ(vn, &u);
	if (result)
	{
		return result;
	}
	of->offset = u.uio_offset;
	return (count - u.uio_resid);
}
#endif

/*
fd is the file descriptor which has been obtained from the call to open. It is an integer value. The values 0, 1, 2 can also be given, for standard input, standard output & standard error, respectively.

buf points to a character array, with content to be written to the file pointed to by fd.

nbytes specifies the number of bytes to be written from the character array into the file pointed to by fd
*/
long sys_write(int fd, userptr_t buf, size_t nbytes)
{
	char *data = (char *)buf;
	size_t i;

	if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
	{
#if OPT_FILE_SYSTEM
		return file_write(fd, buf, nbytes);
#else
		kprintf("sys_write supported only to stdout and stderr\n");
		return -1;
#endif
	}

	// kprintf("Hi! :) I'm sys_write and I'm about to write what you asked:\n");

	for (i = 0; i < nbytes; i++)
	{
		putch(data[i]);
	}

	return i;
}

/* static void
console_send(void *junk, const char *data, size_t len)
{
	size_t i;

	(void)junk;

	for (i = 0; i < len; i++)
	{
		putch(data[i]);
	}
}

void putch(int ch)
{
	struct con_softc *cs = the_console;

	if (cs == NULL)
	{
		putch_delayed(ch);
	}
	else if (curthread->t_in_interrupt ||
			 curthread->t_curspl > 0 ||
			 curcpu->c_spinlocks > 0)
	{
		putch_polled(cs, ch);
	}
	else
	{
		putch_intr(cs, ch);
	}
}
 */

/*
In modern POSIX compliant operating systems, a program that needs to access data from a file stored in a file system uses the read system call. The file is identified by a file descriptor that is normally obtained from a previous call to open. This system call reads in data in bytes, the number of which is specified by the caller, from the file and stores then into a buffer supplied by the calling process.

The read system call takes three arguments:

The file descriptor of the file.
the buffer where the read data is to be stored and
the number of bytes to be read from the file.

The value returned is the number of bytes read (zero indicates end of file) and the file position is advanced by this number. It is not an error if this number is smaller than the number of bytes requested; this may happen for example because fewer bytes are actually available right now (maybe because we were close to end-of-file, or because we are reading from a pipe, or from a terminal), or because the system call was interrupted by a signal.

Alternatively, -1 is returned when an error occurs, in such a case errno is set appropriately and further it is left unspecified whether the file position (if any) changes.
*/
long sys_read(int fd, userptr_t buf, size_t count)
{
	char *data = (char *)buf;
	size_t i;

	if (fd != STDIN_FILENO)
	{
#if OPT_FILE_SYSTEM
		return file_read(fd, buf, count);
#else
		kprintf("sys_read supported only from stdin\n");
		return -1;
#endif
	}

	// kprintf("Hi! :) I'm sys_read and I'm about to read what you asked:\n");

	for (i = 0; i < count; i++)
	{
		data[i] = getch();
		// In the C standard library, the character reading functions such as getchar return a value equal to the symbolic value (macro) EOF to indicate that an end-of-file condition has occurred. The actual value of EOF is implementation-dependent and must be negative (but is commonly −1, such as in glibc[2]). stdio.h non c'è nel kernel.

		if (data[i] < 0)
			return i;
	}

	return i;
}

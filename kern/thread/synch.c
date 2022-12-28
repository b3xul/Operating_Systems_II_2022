/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(*sem));
        if (sem == NULL)
        {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL)
        {
                kfree(sem);
                return NULL;
        }

        sem->sem_wchan = wchan_create(sem->sem_name);
        if (sem->sem_wchan == NULL)
        {
                kfree(sem->sem_name);
                kfree(sem);
                return NULL;
        }

        spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /* wchan_cleanup will assert if anyone's waiting on it */
        spinlock_cleanup(&sem->sem_lock);
        wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

        /* Use the semaphore spinlock to protect the wchan as well. */
        spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0)
        {
                /*
                 *
                 * Note that we don't maintain strict FIFO ordering of
                 * threads going through the semaphore; that is, we
                 * might "get" it on the first try even if other
                 * threads are waiting. Apparently according to some
                 * textbooks semaphores must for some reason have
                 * strict ordering. Too bad. :-)
                 *
                 * Exercise: how would you implement strict FIFO
                 * ordering?
                 */
                wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
        spinlock_release(&sem->sem_lock);
}

void V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
        wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

        spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL)
        {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL)
        {
                kfree(lock);
                return NULL;
        }

        HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

        // add stuff here as needed
#if OPT_LOCKS_SEMAPHORES

        lock->binary_semaphore = sem_create(name, 1); // Binary semaphore with initial value=1
        if (lock->binary_semaphore == NULL)
        {
                kfree(lock->lk_name);
                kfree(lock);
                return NULL;
        }

        spinlock_init(&lock->lk_spin);

        // N.B. it is impossible that someone already holds the lock since we haven't created it yet

        lock->owner = NULL; // The owner is whoever calls lock_acquire, not who calls lock_create
#endif
#if OPT_LOCKS_WCHANS
        lock->lk_wchan = wchan_create(lock->lk_name);
        if (lock->lk_wchan == NULL)
        {
                kfree(lock->lk_name);
                kfree(lock);
                return NULL;
        }
        lock->lk_count = 1; // Like a binary semaphore with initial value=1

        spinlock_init(&lock->lk_spin);

        // N.B. it is impossible that someone already holds the lock since we haven't created it yet

        lock->owner = NULL; // The owner is whoever calls lock_acquire, not who calls lock_create

#endif

        return lock;
}

void lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed
#if OPT_LOCKS_SEMAPHORES

        // when the lock is destroyed, no thread should be holding it.
        KASSERT(lock->owner == NULL);

        spinlock_cleanup(&lock->lk_spin);
        sem_destroy(lock->binary_semaphore);
        kfree(lock->lk_name);
        kfree(lock);
#endif
#if OPT_LOCKS_WCHANS
        KASSERT(lock->owner == NULL);

        spinlock_cleanup(&lock->lk_spin);
        wchan_destroy(lock->lk_wchan);
        kfree(lock->lk_name);
        kfree(lock);

#endif
        (void)lock; // suppress warning until code gets written
}

void lock_acquire(struct lock *lock)
{
#if OPT_LOCKS_SEMAPHORES
        KASSERT(lock != NULL);

        /* Call this (atomically) before waiting for a lock */
        HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        P(lock->binary_semaphore); // Wait until semaphore=1, then enter and decrement it
        /* Call this (atomically) once the lock is acquired */
        HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
        spinlock_acquire(&lock->lk_spin);
        lock->owner = curthread;
        spinlock_release(&lock->lk_spin);

#endif
#if OPT_LOCKS_WCHANS
        /* Call this (atomically) before waiting for a lock */
        HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        KASSERT(lock != NULL);
        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

        /* Use the semaphore spinlock to protect the wchan as well. */
        spinlock_acquire(&lock->lk_spin);
        // Wait until semaphore=1, then enter and decrement it

        while (lock->lk_count == 0)
        {
                /*
                 *
                 * Note that we don't maintain strict FIFO ordering of
                 * threads going through the semaphore; that is, we
                 * might "get" it on the first try even if other
                 * threads are waiting. Apparently according to some
                 * textbooks semaphores must for some reason have
                 * strict ordering. Too bad. :-)
                 *
                 * Exercise: how would you implement strict FIFO
                 * ordering?
                 */
                wchan_sleep(lock->lk_wchan, &lock->lk_spin); // Internally releases lk_spin, and takes it again when it is waken
        }
        KASSERT(lock->lk_count == 1); // We know it's a binary semaphore
        /* Call this (atomically) once the lock is acquired */
        HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
        lock->lk_count--;
        lock->owner = curthread;
        spinlock_release(&lock->lk_spin);

#endif
        (void)lock; // suppress warning until code gets written
}

void lock_release(struct lock *lock)
{
#if OPT_LOCKS_SEMAPHORES
        KASSERT(lock != NULL);

        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&lock->lk_spin);
        lock->owner = NULL;
        spinlock_release(&lock->lk_spin);

        V(lock->binary_semaphore);
        /* Call this (atomically) when the lock is released */
        HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

#endif
#if OPT_LOCKS_WCHANS
        KASSERT(lock != NULL);

        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&lock->lk_spin);
        lock->owner = NULL;

        lock->lk_count++;
        KASSERT(lock->lk_count == 1);
        wchan_wakeone(lock->lk_wchan, &lock->lk_spin);

        spinlock_release(&lock->lk_spin);
        /* Call this (atomically) when the lock is released */
        HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

#endif
        (void)lock; // suppress warning until code gets written
}

bool lock_do_i_hold(struct lock *lock)
{
#if OPT_LOCKS_SEMAPHORES
        spinlock_acquire(&lock->lk_spin);
        bool res = lock->owner == curthread;
        spinlock_release(&lock->lk_spin);
        return res;
#endif
#if OPT_LOCKS_WCHANS
        spinlock_acquire(&lock->lk_spin);
        bool res = lock->owner == curthread;
        spinlock_release(&lock->lk_spin);
        return res;
#endif
        (void)lock; // suppress warning until code gets written

        return true; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV

struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(*cv));
        if (cv == NULL)
        {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name == NULL)
        {
                kfree(cv);
                return NULL;
        }

// add stuff here as needed
#if OPT_CONDITION_VARIABLES
        cv->cv_wchan = wchan_create(cv->cv_name);
        if (cv->cv_wchan == NULL)
        {
                kfree(cv->cv_name);
                kfree(cv);
                return NULL;
        }

        spinlock_init(&cv->cv_spin);
#endif
        return cv;
}

void cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        // add stuff here as needed
#if OPT_CONDITION_VARIABLES

        spinlock_cleanup(&cv->cv_spin);
        wchan_destroy(cv->cv_wchan);
#endif
        kfree(cv->cv_name);
        kfree(cv);
}

void cv_wait(struct cv *cv, struct lock *lock)
{
/*    cv_wait      - Release the supplied lock, go to sleep, and, after
 *                   waking up again, re-acquire the lock. */
#if OPT_CONDITION_VARIABLES
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&cv->cv_spin); // since lock is mine, I can be sure that I am the only one that can acquire the spinlock
        lock_release(lock);
        wchan_sleep(cv->cv_wchan, &cv->cv_spin); // While waiting the spinlock is released, when the wait ends, the spinlock is reacquired
        spinlock_release(&cv->cv_spin);          // since lock is mine again, I can be sure that I am the only one that can release the spinlock! NO! The risk here is that the thread blocks while doing lock_acquire, keeping possession of the spinlock in the meantime! We must first release the spinlock, then acquire the lock again!
        lock_acquire(lock);

#endif
        // Write this
        (void)cv;   // suppress warning until code gets written
        (void)lock; // suppress warning until code gets written
}

void cv_signal(struct cv *cv, struct lock *lock)
{
#if OPT_CONDITION_VARIABLES
        /* cv_signal    - Wake up one thread that's sleeping on this CV. */
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&cv->cv_spin);
        wchan_wakeone(cv->cv_wchan, &cv->cv_spin);
        spinlock_release(&cv->cv_spin);

#endif
        // Write this
        (void)cv;   // suppress warning until code gets written
        (void)lock; // suppress warning until code gets written
}

void cv_broadcast(struct cv *cv, struct lock *lock)
{
#if OPT_CONDITION_VARIABLES
        /* cv_broadcast - Wake up all threads sleeping on this CV. */
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&cv->cv_spin);
        wchan_wakeall(cv->cv_wchan, &cv->cv_spin);
        spinlock_release(&cv->cv_spin);

#endif
        // Write this
        (void)cv;   // suppress warning until code gets written
        (void)lock; // suppress warning until code gets written
}

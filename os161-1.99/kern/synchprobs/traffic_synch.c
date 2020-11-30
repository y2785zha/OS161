#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <queue.h>
/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *intersectionSem;
enum traffic
  {
    North = 0,
    East = 1,
    South = 2,
    West = 3,
    Off = 4
  };
static volatile enum traffic light;
static struct cv *n;
static struct cv *e;
static struct cv *s;
static struct cv *w;
static struct lock *lock;
static struct queue *queue;
static int count;
static bool n_enqueue;
static bool e_enqueue;
static bool s_enqueue;
static bool w_enqueue;
/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  /* intersectionSem = sem_create("intersectionSem",1);
     if (intersectionSem == NULL) {
       panic("could not create intersection semaphore");
     } */
  lock = lock_create("lock");
  lock_acquire(lock);
  light = Off;
  count = 0;
  n_enqueue = false;
  e_enqueue = false;
  s_enqueue = false;
  w_enqueue = false;
  n = cv_create("north");
  e = cv_create("east");
  s = cv_create("south");
  w = cv_create("west");
  queue = q_create(4);
  KASSERT(queue != NULL);
  lock_release(lock);
  
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  /*  KASSERT(intersectionSem != NULL);
    sem_destroy(intersectionSem); */

  cv_destroy(n);
  cv_destroy(e);
  cv_destroy(s);
  cv_destroy(w);
  lock_destroy(lock);
  q_destroy(queue);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
 /* replace this default implementation with your own implementation */
 // (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
 //  KASSERT(intersectionSem != NULL);
 //  P(intersectionSem);
 lock_acquire(lock);
 if (light == Off) {
	light = (enum traffic)(int)origin;
 } else if ((int)light != (int)origin) {
	switch(origin) {
		case north:
		  if (q_empty(queue) || !n_enqueue) {
		  	q_addtail(queue, n);
			n_enqueue = true;
		  }
		  cv_wait(n, lock);
		  break;
		case east:
		  if (q_empty(queue) || !e_enqueue) {
		  	q_addtail(queue, e);
		        e_enqueue = true;
		  }
		  cv_wait(e, lock);
		  break;
		case south:
		  if (q_empty(queue) || !s_enqueue) {
		  	q_addtail(queue, s);
			s_enqueue = true;
		  }
		  cv_wait(s, lock);
		  break;
		case west:
		  if (q_empty(queue) || !w_enqueue) {
	   	  	q_addtail(queue, w);
			w_enqueue = true;
                  }
		  cv_wait(w, lock);
		  break;
	}
 }
 count += 1;
 lock_release(lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
   (void)origin;  /* avoid compiler complaint about unused parameter */
   (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // V(intersectionSem);
  lock_acquire(lock);
  count -= 1;
  if (count <= 0) {
  	if (q_empty(queue)) {
		light = Off;
	} else {
  		struct cv * next = q_remhead(queue);	
  		if (next == n) {
	   		light = North;
			n_enqueue = false;
		} else if (next == e) {
	  		light = East;
			e_enqueue = false;
		} else if (next == s) {
			light = South;
			s_enqueue = false;
		} else if (next == w) {
			light = West;
			w_enqueue = false;
		}
		cv_broadcast(next, lock);
	}
 }
 lock_release(lock);
}

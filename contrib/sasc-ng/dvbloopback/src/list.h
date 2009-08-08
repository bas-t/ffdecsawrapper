#ifndef _LIST_
#define _LIST_

/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized list entries.
 */
#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

struct list_head {
	struct list_head *next, *prev;
        unsigned int priority;
};

#define LIST_HEAD_INIT(name) { &(name), &(name), (unsigned int)-1 }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); (ptr)->priority = (unsigned int)-1;\
} while (0)

/*
 * Insert a new entry between two known consecutive entries. 
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *_new,
			      struct list_head *_prev,
			      struct list_head *_next)
{
	_next->prev = _new;
	_new->next = _next;
	_new->prev = _prev;
	_prev->next = _new;
}

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head *_new, struct list_head *_head)
{
	__list_add(_new, _head, _head->next);
}

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void list_add_tail(struct list_head *_new,
                                 struct list_head *_head)
{
	__list_add(_new, _head->prev, _head);
}

static inline void list_add_priority(struct list_head *_new,
                                     struct list_head *_head,
                                     int priority)
{
	struct list_head *pos = _head;
        _new->priority = priority;
	for (pos = _head->next; pos != _head; pos = pos->next)
	  if((unsigned int)priority <= pos->priority) {
            __list_add(_new, pos->prev, pos);
            return;
          }
        __list_add(_new, _head, _head->next);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = (struct list_head *)LIST_POISON1;
	entry->prev = (struct list_head *)LIST_POISON2;
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define list_entry(ptr, type) ((type *)ptr)

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
        for (pos = (head)->next, n = pos->next; pos != (head); \
                pos = n, n = pos->next)

#define list_add_l(_var, _ll, _lock) {\
  pthread_mutex_lock(_lock);		\
  list_add(_var, _ll);			\
  pthread_mutex_unlock(_lock);		\
}
  
#define pop_entry_from_queue_l(_var, _ll, _type, _lock) {\
  pthread_mutex_lock(_lock);		\
  if(list_empty(_ll))  {			\
    _var = (_type *)calloc(1, sizeof(_type));	\
  } else {					\
    _var = list_entry((_ll)->next, _type);	\
    list_del(&_var->list);			\
  }						\
  pthread_mutex_unlock(_lock);		\
}

#define pop_entry_from_queue(_var, _ll, _type) {\
  if(list_empty(_ll))  {			\
    _var = (_type *)calloc(1, sizeof(_type));	\
  } else {					\
    _var = list_entry((_ll)->next, _type);	\
    list_del(&_var->list);			\
  }						\
}

#define ll_find_elem(elem, lhead, item, value, type) {	\
  struct list_head *lptr;				\
  type *ptr;						\
  elem = NULL;						\
  list_for_each(lptr, &lhead) {				\
    ptr = list_entry(lptr, type);			\
    if(ptr->item == value)				\
      elem = ptr;					\
  }							\
}

#endif

# Poptrie

## About

This is the reference implementation of Poptrie,  "A Compressed Trie with
Population Count for Fast and Scalable Software IP Routing Table Lookup,"
presented in SIGCOMM 2015.


## Author

Hirochika Asai


## License

The use of this software is limited to education, research, and evaluation
purposes only.  Commercial use is strictly prohibited.  For all other uses,
contact the author(s).

## APIs

### Initialization

    NAME
         poptrie_init -- initialize a poptrie control data structure
         
    SYNOPSIS
         struct poptrie *
         poptrie_init(struct poptrie *poptrie, int sz1, int sz0);
         
    DESCRIPTION
         The poptrie_init() function initializes a poptrie control data
         structure specified by the poptrie argument with two memory allocation
         parameters sz1 and sz0.
         
         If the poptrie argument is NULL, a new data structure is allocated and
         initialized.  Otherwise, the data structure specified by the poptrie
         argument is initialized.
         
         The sz1 and sz0 parameters specify the size of pre-allocated entries
         in the arrays of the internal and leaf nodes, respectively.  2 to the
         power of sz1 and sz0 entries are allocated for the internal and leaf
         nodes.  In the implementation, these nodes are managed by the buddy
         system memory allocator.
         
        The recommended parameters for sz1 and sz0 for IP routing tables are 19
        and 22, respectively.

    RETURN VALUES
         Upon successful completion, the poptrie_init() function returns the
         pointer to the initialized poptrie data structure.  Otherwise, it
         returns a NULL value and set errno.  If a non-NULL poptrie argument is
         specified, the returned value shall be the original value of the
         poptrie argument if successful, or a NULL value otherwise.


### Release

    NAME
         poptrie_release -- release the poptrie control data structure
         
    SYNOPSIS
         void
         poptrie_release(struct poptrie *poptrie);
         
    DESCRIPTION
         The poptrie_release() function releases the poptrie control data
         structure specified by the poptrie argument, and other allocated data
         managed by the control data structure.

    RETURN VALUES
         The poptrie_release() function does not return a value.


### Operations for IPv4

    NAME
         poptrie_route_add, poptrie_route_change, poptrie_route_update,
         poptrie_route_del, poptrie_route_lookup -- operate the poptrie for
         IPv4 (32-bit addresses)
         
    SYNOPSIS
         int
         poptrie_route_add(struct poptrie *poptrie, u32 prefix, int len,
         void *nexthop);
         
         int
         poptrie_route_change(struct poptrie *poptrie, u32 prefix, int len,
         void *nexthop);
         
         int
         poptrie_route_update(struct poptrie *poptrie, u32 prefix, int len,
         void *nexthop);
         
         int
         poptrie_route_del(struct poptrie *poptrie, u32 prefix, int len);
         
         void *
         poptrie_lookup(struct poptrie *poptrie, u32 addr);
         
    DESCRIPTION
         The poptrie_route_add(), poptrie_route_change(), and
         poptrie_route_update() functions add, change, and update the next hop
         specified by the nexthop argument to the prefix specified by the prefix
         argument with the prefix length specified by the len argument.  The
         poptrie_route_change() function does not add the next hop when the
         corresponding prefix does not exist.  On the other hand,
         poptrie_route_update() does.
         
         The poptrie_route_del() function deletes the prefix specified by the
         prefix argument with the prefix length of len.
         
         The poptrie_lookup() function looks up the corresponding prefix by
         the specified argument of addr.
         
    RETURN VALUES
         On successful, the poptrie_route_add(), poptrie_route_change(),
         poptrie_route_update(), and poptrie_route_del() functions return a
         value of 0.  Otherwise, they return a value of -1.
         
         The poptrie_lookup() function returns a next hop corresponding to the
         addr argument.  If no matching entry is found, a NULL value is
         returned.


### Operations for IPv6

    NAME
         poptrie6_route_add, poptrie6_route_change, poptrie6_route_update,
         poptrie6_route_del, poptrie6_route_lookup -- operate the poptrie for
         IPv6 (128-bit addresses)
         
    SYNOPSIS
         int
         poptrie6_route_add(struct poptrie *poptrie, __uint128_t prefix,
         int len, void *nexthop);
         
         int
         poptrie6_route_change(struct poptrie *poptrie, __uint128_t prefix,
         int len, void *nexthop);
         
         int
         poptrie6_route_update(struct poptrie *poptrie, __uint128_t prefix,
         int len, void *nexthop);
         
         int
         poptrie6_route_del(struct poptrie *poptrie, __uint128_t prefix,
         int len);
         
         void *
         poptrie6_lookup(struct poptrie *poptrie, __uint128_t addr);
         
    DESCRIPTION
         The poptrie6_route_add(), poptrie6_route_change(), and
         poptrie6_route_update() functions add, change, and update the next hop
         specified by the nexthop argument to the prefix specified by the prefix
         argument with the prefix length specified by the len argument.  The
         poptrie6_route_change() function does not add the next hop when the
         corresponding prefix does not exist.  On the other hand,
         poptrie6_route_update() does.
         
         The poptrie6_route_del() function deletes the prefix specified by the
         prefix argument with the prefix length of len.
         
         The poptrie6_lookup() function looks up the corresponding prefix by
         the specified argument of addr.
         
    RETURN VALUES
         On successful, the poptrie6_route_add(), poptrie6_route_change(),
         poptrie6_route_update(), and poptrie6_route_del() functions return a
         value of 0.  Otherwise, they return a value of -1.
         
         The poptrie6_lookup() function returns a next hop corresponding to the
         addr argument.  If no matching entry is found, a NULL value is
         returned.


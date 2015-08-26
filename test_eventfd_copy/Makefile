

test_eventfd_copy: test_eventfd_copy.c \
                   ../lib/librte_vhost/vhost_cuse/eventfd_copy.c
	gcc -o $@ $^ \
	    -I . \
	    -I ../lib/librte_vhost/ \
            -I ../lib/librte_vhost/vhost_cuse/

COMPONENTS=collectd archived
RECENT_COMPONENT=$(shell ls -dt $(COMPONENTS) | head -1 )

all :
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) $(MAKEFLAGS) all && ) echo done

clean:
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) $(MAKEFLAGS) clean && ) echo done

quick:
	$(MAKE) -C $(RECENT_COMPONENT) $(MAKEFLAGS)


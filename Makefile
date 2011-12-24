COMPONENTS=collectd collect-smsd archived archived-monitor convert migrationd

RECENT_COMPONENT=$(shell ls -dt $(COMPONENTS) | head -1 )

all :
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) all && ) echo done

clean:
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) clean && ) echo done

quick:
	$(MAKE) -C $(RECENT_COMPONENT)


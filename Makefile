COMPONENTS=collectd collect-smsd archived convert
#COMPONENTS=collectd archived migrationd convert
RECENT_COMPONENT=$(shell ls -dt $(COMPONENTS) | head -1 )

all :
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) all && ) echo done

clean:
	$(foreach dir,$(COMPONENTS),$(MAKE) -C $(dir) clean && ) echo done

quick:
	$(MAKE) -C $(RECENT_COMPONENT)


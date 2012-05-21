COMPONENTS=collectd collect-smsd archived archived-monitor convert

RECENT_COMPONENT=$(shell ls -dt $(COMPONENTS) | head -1 )

ifdef DEV_LIBS_BASEDIR
DEV_LIBS=DEV_LIBS_BASEDIR=$(DEV_LIBS_BASEDIR)
endif

all :
	$(foreach dir,$(COMPONENTS),$(MAKE) $(DEV_LIBS) -C $(dir) all && ) echo done

clean:
	$(foreach dir,$(COMPONENTS),$(MAKE) $(DEV_LIBS) -C $(dir) clean && ) echo done

quick:
	$(MAKE) -C $(RECENT_COMPONENT)


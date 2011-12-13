#include "archivable.hpp"

namespace archiving
{
	// default implementation for archivable destruct handler
	void on_archivable_destruct_default(class archivable *ptr)
	{
	}

	archivable_fn on_archivable_destruct = boost::bind(on_archivable_destruct_default, _1);
}

// unholy mess of single-header implementations woe
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

// uhhh... please?
#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t nel, size_t width, int (*cmp)(const void *, const void *))
{
	void *try;
	int sign;
	while (nel > 0) {
		try = (char *)base + width*(nel/2);
		sign = cmp(key, try);
		if (sign < 0) {
			nel /= 2;
		} else if (sign > 0) {
			base = (char *)try + width;
			nel -= nel/2+1;
		} else {
			return try;
		}
	}
	return NULL;
}

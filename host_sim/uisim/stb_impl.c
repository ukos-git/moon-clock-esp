/* Pull in the stb_image_write implementation (header-only lib) so the
 * snapshot PNG writer links. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

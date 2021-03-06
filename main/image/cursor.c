#include <stdint.h>

#include "image.h"

const image_t image_cursor = {
    .width = 24,
    .height = 15,
    .transparent_color = 0x1ff8,
    .data = {
        0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x0321, 0xe218, 0xe318, 0xe318, 0xe320, 0xe318, 0xe318, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0x1ff8, 0x1ff8, 0xe318, 0xc318, 0x0421, 0x1ff8, 0xc318, 0x0421, 0xc318, 0x76b5, 0x59ce, 0x59ce, 0x59ce, 0x518c, 0x518c, 0x0421, 0xc318, 0x0421, 0x0421, 0xe320, 0xe320, 0xe320, 0x0421, 0x4529, 
        0x1ff8, 0x0321, 0x6631, 0x6e73, 0x4e6b, 0xe420, 0x6631, 0x528c, 0x76b5, 0x5def, 0x5def, 0x5def, 0x3def, 0x3def, 0xdbde, 0xbbde, 0xbbd6, 0xdbde, 0xbbde, 0xbade, 0xbbde, 0x59ce, 0x518c, 0x0421, 
        0xe318, 0xe420, 0xf4a4, 0xdfff, 0xbbde, 0x4d6b, 0xdbde, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x5def, 0x3def, 0x3def, 0xdbde, 0x518c, 0xe320, 
        0xe318, 0xcb5a, 0xdfff, 0xffff, 0x58ce, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x3def, 0x5def, 0x5def, 0x5def, 0x1ce7, 0xbbde, 0xbbde, 0x7194, 0x0421, 0xc318, 
        0xe320, 0x76b5, 0xffff, 0x3ce7, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0xdfff, 0xdfff, 0x38ce, 0x59ce, 0x38ce, 0x39ce, 0x39ce, 0x97b5, 0xf4a4, 0x6631, 0xe320, 0x1ff8, 
        0xe318, 0xdbde, 0xffff, 0xffff, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0xffff, 0xffff, 0x59ce, 0xffff, 0x59ce, 0x4d6b, 0xe420, 0xe420, 0xe420, 0xc420, 0xe420, 0xe320, 0x1ff8, 0x1ff8, 
        0xc318, 0xbbde, 0xffff, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0x5def, 0xffff, 0x5def, 0x76b5, 0x3def, 0x6a52, 0xe420, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0xe320, 0x39ce, 0xdbde, 0x38ce, 0xbade, 0x3ce7, 0x5def, 0xbad6, 0xffff, 0xdfff, 0xffff, 0xdbde, 0x5def, 0x76b5, 0xbbde, 0xcb5a, 0x4629, 0xe420, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0xe320, 0x75b5, 0x38ce, 0x75b5, 0x75b5, 0x75b5, 0x75b5, 0x75b5, 0x95b5, 0x3ce7, 0x3cef, 0x96b5, 0xbade, 0x7194, 0x76b5, 0xf083, 0xcf7b, 0xe420, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0xe320, 0x7194, 0x76b5, 0xef83, 0xef83, 0xef83, 0xef83, 0xef83, 0xf4a4, 0x59ce, 0xbade, 0xf3a4, 0x96b5, 0xf083, 0x7194, 0x39ce, 0x96b5, 0xc320, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0x0421, 0xc839, 0xf4a4, 0x5294, 0x6a52, 0xf083, 0x4e73, 0x1084, 0xbbde, 0xbbde, 0x14a5, 0x6d73, 0x718c, 0xe741, 0xf4a4, 0x59ce, 0xf4a4, 0xe420, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0x0421, 0xe420, 0xe841, 0xe839, 0xc418, 0x6631, 0xe420, 0xcc5a, 0x728c, 0xf083, 0x694a, 0x6531, 0xe320, 0xe741, 0x96b5, 0x56ad, 0xe841, 0xc320, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0x1ff8, 0x0421, 0xe318, 0x0321, 0xc318, 0x0321, 0xe318, 0xe318, 0xc218, 0x0321, 0xe320, 0xe318, 0xe318, 0xe420, 0xc739, 0x6631, 0xc318, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
        0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0xc318, 0xe318, 0xe318, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 
    }
};
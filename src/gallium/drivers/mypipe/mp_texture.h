#ifndef MP_TEXTURE_H
#define MP_TEXTURE_H

#include "pipe/p_state.h"

void mypipe_init_screen_texture_funcs(struct pipe_screen *screen);
void mypipe_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *pt);

struct mypipe_resource{
    struct pipe_resource base;
    unsigned int stride[15];
    unsigned int img_stride[15];
    unsigned int level_offset[15];

    struct sw_displaytarget *dt;

    void *data;

    
    bool pot;
    unsigned timestamp;
};

struct mypipe_transfer{
    struct pipe_transfer base;
    unsigned long offset;
};

static inline struct mypipe_transfer *mypipe_transfer(struct pipe_transfer *pt){
    return (struct mypipe_transfer *)pt;
}

static inline struct mypipe_resource *mypipe_resource(struct pipe_resource *pt){
    return (struct mypipe_resource*)pt;
}

unsigned mypipe_get_tex_image_offset(const struct mypipe_resource *mpr, unsigned level, unsigned layer);

#endif
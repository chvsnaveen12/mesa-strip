#ifndef MP_DRAW_H
#define MP_DRAW_H

struct mypipe_context;
struct pipe_draw_info;
struct pipe_draw_start_count_bias;

void mypipe_do_draw_vbo(struct mypipe_context *mypipe, const struct pipe_draw_info *info,
                        const struct pipe_draw_start_count_bias *draws,
                        unsigned num_draws);

#endif
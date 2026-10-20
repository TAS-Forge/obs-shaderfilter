/* OBS graphics contract mock, not a GPU implementation. The runner inserts
 * unmodified function bodies from obs-shaderfilter.c at the marker below. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define UNUSED_PARAMETER(x) (void)(x)
#define OBS_COUNTOF(x) (sizeof(x) / sizeof((x)[0]))
#define OBS_NO_DIRECT_RENDERING 0
#define OBS_EFFECT_DEFAULT 0
#define GS_ZS_NONE 0
#define GS_CLEAR_COLOR 1
#define GS_TRISTRIP 0
enum gs_color_space { GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED, GS_CS_709_SCRGB };
enum gs_color_format { GS_RGBA, GS_RGBA16F };
enum { GS_BLEND_ONE, GS_BLEND_ZERO, GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA };
enum { GS_SHADER_PARAM_FLOAT, GS_SHADER_PARAM_INT, GS_SHADER_PARAM_VEC2,
       GS_SHADER_PARAM_VEC3, GS_SHADER_PARAM_VEC4 };
struct vec2 { float x,y; }; struct vec3 { float x,y,z; }; struct vec4 { float x,y,z,w; };
struct gs_vb_data { int unused; };
typedef struct { float rgba[4]; } gs_texture_t;
typedef struct texrender {
    enum gs_color_format format;
    enum gs_color_space space, previous_space;
    struct texrender *previous_target;
    gs_texture_t texture;
    bool rendered, has_texture;
} gs_texrender_t;
typedef struct { gs_texture_t *texture; bool value; } gs_eparam_t;
typedef struct { gs_eparam_t image, output, convert; int loops; } gs_effect_t;
typedef struct { int unused; } obs_source_t;
struct effect_param_data {
    gs_eparam_t *param; int type; bool has_default;
    struct { char *array; } name;
    union value { double f; long long i; struct vec2 vec2; struct vec3 vec3; struct vec4 vec4; } value, default_value;
};
struct shader_filter_data {
    gs_texrender_t *input_texrender, *previous_input_texrender, *output_texrender, *previous_output_texrender;
    enum gs_color_space render_space; bool render_space_valid;
    bool input_rendered, output_rendered, transition, use_pm_alpha, use_template, rendering;
    obs_source_t *context;
    gs_effect_t *effect, *output_effect;
    gs_eparam_t *param_image, *param_output_image, *param_previous_image, *param_previous_output;
    void *sprite_buffer;
    int total_width, total_height;
    struct { size_t num; struct effect_param_data *array; } stored_param_list;
};
static enum gs_color_space source_space, current_space;
static gs_texrender_t *current_target;
static gs_effect_t base_effect, shader_effect, output_effect;
static gs_effect_t *active_effect;
static bool framebuffer_srgb, fail_begin, fail_process;
static int creates, destroys, skips, blend_depth, blend_src, blend_stack[8];
static float incoming[4], composed[4];
static bool pm_input;
static float (*move_get_transition_filter)(obs_source_t *, obs_source_t **) = NULL;

static float decode(float x) { return x <= .04045f ? x/12.92f : powf((x+.055f)/1.055f, 2.4f); }
static float encode(float x) { return x <= .0031308f ? 12.92f*x : 1.055f*powf(x,1/2.4f)-.055f; }
static enum gs_color_format gs_get_format_from_space(enum gs_color_space s) { return s==GS_CS_SRGB ? GS_RGBA : GS_RGBA16F; }
static gs_texrender_t *gs_texrender_create(enum gs_color_format f, int z) {
    (void)z; creates++; gs_texrender_t *r=calloc(1,sizeof(*r)); assert(r); r->format=f; return r;
}
static void gs_texrender_destroy(gs_texrender_t *r) { if(r) { destroys++; free(r); } }
static enum gs_color_format gs_texrender_get_format(gs_texrender_t *r) { return r->format; }
static void gs_texrender_reset(gs_texrender_t *r) { r->rendered=false; }
static gs_texture_t *gs_texrender_get_texture(gs_texrender_t *r) { return r && r->has_texture ? &r->texture : NULL; }
static bool gs_texrender_begin_with_color_space(gs_texrender_t *r,int w,int h,enum gs_color_space s) {
    if(!r || r->rendered || !w || !h || fail_begin) return false;
    r->previous_space=current_space; r->space=s; r->previous_target=current_target;
    current_space=s; current_target=r; r->has_texture=true; return true;
}
static void gs_texrender_end(gs_texrender_t *r) { current_space=r->previous_space; current_target=r->previous_target; r->rendered=true; }
static bool gs_framebuffer_srgb_enabled(void) { return framebuffer_srgb; }
static void gs_enable_framebuffer_srgb(bool v) { framebuffer_srgb=v; }
static void gs_blend_state_push(void) { blend_stack[blend_depth++]=blend_src; }
static void gs_blend_state_pop(void) { blend_src=blend_stack[--blend_depth]; }
static void gs_reset_blend_state(void) { blend_src=GS_BLEND_SRCALPHA; }
static void gs_enable_blending(bool b) { (void)b; }
static void gs_blend_function(int s,int d) { (void)d; blend_src=s; }
static void gs_blend_function_separate(int s,int d,int a,int b) { (void)d;(void)a;(void)b; blend_src=s; }
static void gs_ortho(float a,float b,float c,float d,float e,float f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static void vec4_zero(struct vec4 *v) { memset(v,0,sizeof(*v)); }
static void gs_clear(int a,struct vec4 *v,float b,int c) { (void)a;(void)v;(void)b;(void)c; memset(&current_target->texture,0,sizeof(gs_texture_t)); }
static obs_source_t *obs_filter_get_target(obs_source_t *s) { return s; }
static enum gs_color_space obs_source_get_color_space(obs_source_t *s,size_t n,const enum gs_color_space *p) { (void)s;(void)n;(void)p; return source_space; }
static gs_effect_t *obs_get_base_effect(int e) { (void)e; return &base_effect; }
static bool obs_source_process_filter_begin_with_color_space(obs_source_t *s,enum gs_color_format f,enum gs_color_space c,int a) {
    (void)s;(void)a; assert(f==gs_get_format_from_space(c)); return !fail_process;
}
static void store(const float *rgba) {
    for(int i=0;i<4;i++) {
        float x=rgba[i];
        if(current_target->format==GS_RGBA) {
            if(i<3 && framebuffer_srgb) x=encode(x);
            x=roundf(fminf(1,fmaxf(0,x))*255)/255;
        }
        current_target->texture.rgba[i]=x;
    }
}
static void obs_source_process_filter_tech_end(obs_source_t *s,gs_effect_t *e,int w,int h,const char *tech) {
    (void)s;(void)e;(void)w;(void)h;
    bool saved=framebuffer_srgb; framebuffer_srgb=true;
    float rgba[4]; memcpy(rgba,incoming,sizeof(rgba));
    if(!strcmp(tech,"DrawAlphaDivide")) for(int i=0;i<3;i++) rgba[i]=rgba[3]>0 ? rgba[i]/rgba[3] : 0;
    store(rgba); framebuffer_srgb=saved;
}
static gs_eparam_t *gs_effect_get_param_by_name(gs_effect_t *e,const char *n) { return !strcmp(n,"convert_linear") ? &e->convert : &e->image; }
static void gs_effect_set_texture(gs_eparam_t *p,gs_texture_t *t) { if(p) p->texture=t; }
static void gs_effect_set_bool(gs_eparam_t *p,bool v) { p->value=v; }
static void obs_source_process_filter_end(obs_source_t *s,gs_effect_t *e,int w,int h) {
    (void)s;(void)w;(void)h; assert(e->output.texture);
    float *rgba=e->output.texture->rgba;
    for(int i=0;i<4;i++) {
        float v=rgba[i]; if(i<3 && e->convert.value) v=decode(v);
        composed[i]=v*(i<3 && blend_src==GS_BLEND_SRCALPHA ? rgba[3] : 1);
    }
}
static void shader_filter_set_effect_params(struct shader_filter_data *f) { (void)f; }
static void *obs_obj_get_data(obs_source_t *s) { (void)s; return NULL; }
static void gs_effect_set_float(gs_eparam_t *p,float f) { (void)p;(void)f; }
static void gs_effect_set_int(gs_eparam_t *p,int i) { (void)p;(void)i; }
static void gs_effect_set_vec2(gs_eparam_t *p,struct vec2 *v) { (void)p;(void)v; }
static void gs_effect_set_vec3(gs_eparam_t *p,struct vec3 *v) { (void)p;(void)v; }
static void gs_effect_set_vec4(gs_eparam_t *p,struct vec4 *v) { (void)p;(void)v; }
static bool gs_effect_loop(gs_effect_t *e,const char *n) { (void)n; active_effect=e; return e->loops++%2==0; }
static void gs_draw_sprite(gs_texture_t *t,int f,int w,int h) { (void)t;(void)f;(void)w;(void)h; store(active_effect->image.texture->rgba); }
static void load_sprite_buffer(struct shader_filter_data *f) { (void)f; }
static struct gs_vb_data *gs_vertexbuffer_get_data(void *p) { return p; }
static void build_sprite_norm(struct gs_vb_data *d,float w,float h) { (void)d;(void)w;(void)h; }
static void gs_vertexbuffer_flush(void *p) { (void)p; }
static void gs_load_vertexbuffer(void *p) { (void)p; }
static void gs_load_indexbuffer(void *p) { (void)p; }
static void gs_draw(int a,int b,int c) { (void)a;(void)b;(void)c; }
static void obs_source_skip_video_filter(obs_source_t *s) { (void)s; skips++; }

/* ACTUAL_PLUGIN_FUNCTIONS */

static void release_buffers(struct shader_filter_data *f) {
    gs_texrender_destroy(f->input_texrender); gs_texrender_destroy(f->previous_input_texrender);
    gs_texrender_destroy(f->output_texrender); gs_texrender_destroy(f->previous_output_texrender);
    f->input_texrender=f->previous_input_texrender=f->output_texrender=f->previous_output_texrender=NULL;
}
int main(void) {
    struct shader_filter_data f={0};
    f.effect=&shader_effect; f.output_effect=&output_effect; f.use_template=true;
    f.param_image=&shader_effect.image; f.param_output_image=&output_effect.output;
    f.total_width=f.total_height=16;
    const float values[]={-.125f,0,.001f,10/300.f,100/300.f,1,2,1000/300.f};
    int cases=0;
    for(int space=GS_CS_SRGB;space<=GS_CS_709_SCRGB;space++) {
        source_space=(enum gs_color_space)space;
        for(int pm=0;pm<2;pm++) for(int srgb=0;srgb<2;srgb++) for(int alpha=0;alpha<3;alpha++) {
            for(size_t k=0;k<OBS_COUNTOF(values);k++) {
                float v=values[k], a=alpha*.5f;
                if(space==GS_CS_SRGB && (v<0 || v>1)) continue;
                pm_input=pm!=0; f.use_pm_alpha=pm_input;
                for(int i=0;i<3;i++) incoming[i]=v*a;
                incoming[3]=a; framebuffer_srgb=srgb!=0;
                f.input_rendered=f.output_rendered=false;
                shader_filter_render(&f,NULL);
                assert(f.output_rendered && !f.rendering);
                assert(f.input_texrender->space==source_space && f.output_texrender->space==source_space);
                assert(f.input_texrender->format==gs_get_format_from_space(source_space));
                assert(f.output_texrender->format==gs_get_format_from_space(source_space));
                assert(framebuffer_srgb==(srgb!=0) && blend_depth==0 && current_target==NULL);
                for(int i=0;i<4;i++) assert(fabsf(composed[i]-incoming[i])<(space==GS_CS_SRGB ? .009f : 2e-6f));
                assert(output_effect.convert.value==(space==GS_CS_SRGB));
                cases++;
            }
        }
    }
    /* A representation change invalidates both ping-pong history pairs,
       even when both representations use the same GS_RGBA16F format. */
    reset_render_color_space(&f,GS_CS_709_EXTENDED);
    f.input_texrender=gs_texrender_create(GS_RGBA16F,0);
    f.output_texrender=gs_texrender_create(GS_RGBA16F,0);
    f.previous_input_texrender=gs_texrender_create(GS_RGBA16F,0);
    f.previous_output_texrender=gs_texrender_create(GS_RGBA16F,0);
    int before=destroys;
    reset_render_color_space(&f,GS_CS_SRGB_16F);
    assert(destroys==before+4 && !f.input_texrender && !f.previous_input_texrender && !f.output_texrender && !f.previous_output_texrender);
    gs_texrender_t *r=gs_texrender_create(GS_RGBA,0);
    r=create_or_reset_texrender(r,GS_RGBA16F); assert(gs_texrender_get_format(r)==GS_RGBA16F);
    before=creates; r=create_or_reset_texrender(r,GS_RGBA16F); assert(creates==before); gs_texrender_destroy(r);
    gs_eparam_t previous_image={0}, previous_output={0};
    f.param_previous_image=&previous_image; f.param_previous_output=&previous_output;
    source_space=GS_CS_709_EXTENDED;
    for(int frame=0;frame<3;frame++) {
        f.input_rendered=f.output_rendered=false;
        shader_filter_render(&f,NULL);
        assert(f.output_rendered);
    }
    assert(previous_image.texture && previous_output.texture);
    source_space=GS_CS_SRGB_16F; shader_filter_render(&f,NULL);
    assert(!previous_image.texture && !previous_output.texture);
    f.param_previous_image=f.param_previous_output=NULL;
    /* Same-frame cache must not survive an HDR -> SDR change. */
    source_space=GS_CS_709_EXTENDED; shader_filter_render(&f,NULL);
    source_space=GS_CS_SRGB; shader_filter_render(&f,NULL); assert(f.render_space==GS_CS_SRGB && f.input_texrender->format==GS_RGBA);
    /* Allocation/target begin failures restore caller state and do not cache stale output. */
    f.input_rendered=f.output_rendered=false; fail_begin=true; before=skips;
    shader_filter_render(&f,NULL); assert(skips==before+1 && !f.output_rendered); fail_begin=false;
    assert(get_input_source(&f)); framebuffer_srgb=true; fail_begin=true;
    assert(!render_shader(&f,0,NULL)); assert(framebuffer_srgb && blend_depth==0); fail_begin=false;
    fail_process=true; f.input_rendered=false; assert(!get_input_source(&f)); fail_process=false;
    release_buffers(&f); assert(creates==destroys);
    printf("PASS: %d identity/alpha/state cases; format changes, history invalidation, cache changes, failure restoration, resource balance\n",cases);
    return 0;
}

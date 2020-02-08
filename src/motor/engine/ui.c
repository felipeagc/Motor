#include <motor/engine/ui.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <motor/base/math.h>
#include <motor/base/array.h>
#include <motor/base/util.h>
#include <motor/base/hashmap.h>
#include <motor/base/allocator.h>
#include <motor/graphics/renderer.h>
#include <motor/graphics/window.h>
#include <motor/engine/asset_manager.h>
#include <motor/engine/assets/pipeline_asset.h>
#include <motor/engine/assets/font_asset.h>
#include "assets/font_asset.inl"

enum
{
    DEFAULT_FONT_HEIGHT = 32
};

static FontAtlas *get_atlas(MtFontAsset *asset, uint32_t height);

typedef struct UIState
{
    MtImage *image;
    MtSampler *sampler;
    MtViewport viewport;
} UIState;

typedef struct UICommand
{
    UIState state;
    /* array */ MtUIVertex *vertices;
    /* array */ uint16_t *indices;
} UICommand;

struct MtUIRenderer
{
    MtAllocator *alloc;
    MtWindow *window;
    MtEngine *engine;

    MtSampler *sampler;

    MtPipelineAsset *pipeline;
    MtFontAsset *default_font;

    int32_t mouse_x;
    int32_t mouse_y;
    uint32_t mouse_state;

    uint64_t active_id;
    uint64_t hot_id;

    Vec2 pos;
    Vec3 color;

    MtFontAsset *font;
    uint32_t font_height;

    UIState state;
    /*array*/ UICommand *commands;
};

static UICommand *current_command(MtUIRenderer *ui)
{
    UICommand *last_cmd = NULL;
    if (mt_array_size(ui->commands) == 0)
    {
        UICommand cmd = {0};
        mt_array_push(ui->alloc, ui->commands, cmd);
        last_cmd        = mt_array_last(ui->commands);
        last_cmd->state = ui->state;
    }

    last_cmd = mt_array_last(ui->commands);

    if (memcmp(&ui->state, &last_cmd->state, sizeof(UIState)) != 0)
    {
        UICommand cmd = {0};
        mt_array_push(ui->alloc, ui->commands, cmd);
        last_cmd        = mt_array_last(ui->commands);
        last_cmd->state = ui->state;
    }

    return last_cmd;
}

MtUIRenderer *mt_ui_create(MtAllocator *alloc, MtWindow *window, MtAssetManager *asset_manager)
{
    MtUIRenderer *ui = mt_alloc(alloc, sizeof(MtUIRenderer));
    memset(ui, 0, sizeof(*ui));

    ui->alloc  = alloc;
    ui->window = window;
    ui->engine = asset_manager->engine;

    ui->pipeline =
        (MtPipelineAsset *)mt_asset_manager_load(asset_manager, "../assets/shaders/ui.glsl");
    assert(ui->pipeline);

    ui->default_font = (MtFontAsset *)mt_asset_manager_load(
        asset_manager, "../assets/fonts/SourceSansPro-Regular.ttf");
    assert(ui->default_font);

    ui->font_height = DEFAULT_FONT_HEIGHT;
    ui->pos         = V2(0.0f, 0.0f);
    ui->color       = V3(1.0f, 1.0f, 1.0f);

    ui->sampler = mt_render.create_sampler(
        ui->engine->device,
        &(MtSamplerCreateInfo){
            .anisotropy   = false,
            .mag_filter   = MT_FILTER_NEAREST,
            .min_filter   = MT_FILTER_NEAREST,
            .address_mode = MT_SAMPLER_ADDRESS_MODE_REPEAT,
            .border_color = MT_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        });

    return ui;
}

void mt_ui_destroy(MtUIRenderer *ui)
{
    mt_render.destroy_sampler(ui->engine->device, ui->sampler);

    for (uint32_t i = 0; i < mt_array_size(ui->commands); ++i)
    {
        mt_array_free(ui->alloc, ui->commands[i].vertices);
        mt_array_free(ui->alloc, ui->commands[i].indices);
    }
    mt_array_free(ui->alloc, ui->commands);
    mt_free(ui->alloc, ui);
}

void mt_ui_on_event(MtUIRenderer *ui, MtEvent *event)
{
    switch (event->type)
    {
        case MT_EVENT_CURSOR_MOVED:
        {
            ui->mouse_x = event->pos.x;
            ui->mouse_y = event->pos.y;
            break;
        }
        case MT_EVENT_BUTTON_PRESSED:
        {
            ui->mouse_state = MT_INPUT_STATE_PRESS;
            break;
        }
        case MT_EVENT_BUTTON_RELEASED:
        {
            ui->mouse_state = MT_INPUT_STATE_RELEASE;
            ui->active_id   = 0;
            break;
        }
        case MT_EVENT_KEY_PRESSED:
        {
            break;
        }
        default: break;
    }
}

void mt_ui_set_font(MtUIRenderer *ui, MtFontAsset *font)
{
    ui->font = font;
}

void mt_ui_set_font_size(MtUIRenderer *ui, uint32_t height)
{
    ui->font_height = height;
}

void mt_ui_set_pos(MtUIRenderer *ui, Vec2 pos)
{
    ui->pos = pos;
}

void mt_ui_set_color(MtUIRenderer *ui, Vec3 color)
{
    ui->color = color;
}

static bool region_hit(MtUIRenderer *ui, float x, float y, float w, float h)
{
    return (ui->mouse_x >= x && ui->mouse_x <= x + w) && (ui->mouse_y >= y && ui->mouse_y <= y + h);
}

static float text_width(MtUIRenderer *ui, const char *text)
{
    FontAtlas *atlas = get_atlas(ui->font, ui->font_height);

    float last_x = 0.0f;

    uint32_t ci = 0;
    while (text[ci])
    {
        stbtt_bakedchar *cd = &atlas->chardata[(uint32_t)text[ci]];
        last_x += cd->xadvance;
        ci++;
    }

    return last_x;
}

void mt_ui_print(MtUIRenderer *ui, const char *text)
{
    FontAtlas *atlas  = get_atlas(ui->font, ui->font_height);
    ui->state.image   = atlas->image;
    ui->state.sampler = ui->sampler;
    UICommand *cmd    = current_command(ui);

    ui->pos.y += atlas->height;

    float last_x = 0.0f;

    uint32_t ci = 0;
    while (text[ci])
    {
        stbtt_bakedchar *cd = &atlas->chardata[(uint32_t)text[ci]];

        uint16_t first_vertex = mt_array_size(cmd->vertices);

        float x0 = (float)cd->x0 / (float)atlas->dim;
        float x1 = (float)cd->x1 / (float)atlas->dim;
        float y0 = (float)cd->y0 / (float)atlas->dim;
        float y1 = (float)cd->y1 / (float)atlas->dim;

        float cw = (float)(cd->x1 - cd->x0);
        float ch = (float)(cd->y1 - cd->y0);

        MtUIVertex vertices[4] = {
            {
                {last_x + ui->pos.x, ui->pos.y + ch + cd->yoff},
                {x0, y1},
                ui->color,
            }, // Bottom Left
            {
                {last_x + ui->pos.x, ui->pos.y + cd->yoff},
                {x0, y0},
                ui->color,
            }, // Top Left
            {
                {last_x + cw + ui->pos.x, ui->pos.y + cd->yoff},
                {x1, y0},
                ui->color,
            }, // Top Right
            {
                {last_x + cw + ui->pos.x, ui->pos.y + ch + cd->yoff},
                {x1, y1},
                ui->color,
            }, // Bottom Right
        };

        mt_array_push(ui->alloc, cmd->vertices, vertices[0]);
        mt_array_push(ui->alloc, cmd->vertices, vertices[1]);
        mt_array_push(ui->alloc, cmd->vertices, vertices[2]);
        mt_array_push(ui->alloc, cmd->vertices, vertices[3]);

        mt_array_push(ui->alloc, cmd->indices, first_vertex);
        mt_array_push(ui->alloc, cmd->indices, first_vertex + 1);
        mt_array_push(ui->alloc, cmd->indices, first_vertex + 2);
        mt_array_push(ui->alloc, cmd->indices, first_vertex + 2);
        mt_array_push(ui->alloc, cmd->indices, first_vertex + 3);
        mt_array_push(ui->alloc, cmd->indices, first_vertex);

        last_x += cd->xadvance;

        ci++;
    }

    cmd->state = ui->state;
}

void mt_ui_printf(MtUIRenderer *ui, const char *fmt, ...)
{
    char buf[512];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, vl);
    va_end(vl);

    buf[sizeof(buf) - 1] = '\0';

    mt_ui_print(ui, buf);
}

static void add_rect(MtUIRenderer *ui, float w, float h)
{
    UICommand *cmd = current_command(ui);

    float x = ui->pos.x;
    float y = ui->pos.y;

    uint16_t first_index = (uint16_t)mt_array_size(cmd->vertices);

    MtUIVertex vertices[4] = {
        {V2(x, y), V2(0.0f, 0.0f), ui->color},
        {V2(x + w, y), V2(1.0f, 0.0f), ui->color},
        {V2(x + w, y + h), V2(1.0f, 1.0f), ui->color},
        {V2(x, y + h), V2(0.0f, 1.0f), ui->color},
    };

    mt_array_push(ui->alloc, cmd->vertices, vertices[0]);
    mt_array_push(ui->alloc, cmd->vertices, vertices[1]);
    mt_array_push(ui->alloc, cmd->vertices, vertices[2]);
    mt_array_push(ui->alloc, cmd->vertices, vertices[3]);

    mt_array_push(ui->alloc, cmd->indices, first_index);
    mt_array_push(ui->alloc, cmd->indices, first_index + 1);
    mt_array_push(ui->alloc, cmd->indices, first_index + 2);
    mt_array_push(ui->alloc, cmd->indices, first_index + 2);
    mt_array_push(ui->alloc, cmd->indices, first_index + 3);
    mt_array_push(ui->alloc, cmd->indices, first_index);

    ui->pos.y += h;
}

void mt_ui_rect(MtUIRenderer *ui, float w, float h)
{
    ui->state.image   = ui->engine->white_image;
    ui->state.sampler = ui->sampler;
    add_rect(ui, w, h);
}

void mt_ui_image(MtUIRenderer *ui, MtImage *image, float w, float h)
{
    ui->state.image   = image;
    ui->state.sampler = ui->sampler;
    add_rect(ui, w, h);
}

bool mt_ui_button(MtUIRenderer *ui, const char *text)
{
    uint64_t id = mt_hash_str(text);

    bool result     = false;
    Vec3 prev_color = ui->color;

    static const float pad = 10.0f;

    FontAtlas *atlas = get_atlas(ui->font, ui->font_height);

    float text_w = text_width(ui, text);
    float text_h = atlas->height;

    float w = text_w + pad * 2.0f;
    float h = text_h + pad * 2.0f;

    float x = ui->pos.x;
    float y = ui->pos.y;

    if (region_hit(ui, x, y, w, h))
    {
        if (ui->active_id == 0 || ui->active_id == id)
        {
            ui->hot_id = id;
        }

        if (ui->active_id == 0 && ui->mouse_state == MT_INPUT_STATE_PRESS)
        {
            result        = true;
            ui->active_id = id;
        }
    }

    if (ui->active_id == id)
    {
        ui->color = V3(1, 0, 0);
    }
    else if (ui->hot_id == id)
    {
        ui->color = V3(0, 1, 0);
    }
    else
    {
        ui->color = V3(0, 0, 0);
    }

    Vec2 text_pos = ui->pos;

    mt_ui_rect(ui, w, h);
    Vec2 after_button_pos = ui->pos;

    ui->color = V3(1, 1, 1);
    mt_ui_set_pos(ui, V2(text_pos.x + pad, text_pos.y + pad));
    mt_ui_printf(ui, text);

    mt_ui_set_pos(ui, after_button_pos);

    ui->color = prev_color;

    return result;
}

void mt_ui_begin(MtUIRenderer *ui, MtViewport *viewport)
{
    memset(&ui->state, 0, sizeof(ui->state));
    ui->state.viewport = *viewport;

    ui->pos   = V2(0.0f, 0.0f);
    ui->color = V3(1.0f, 1.0f, 1.0f);

    ui->font        = ui->default_font;
    ui->font_height = DEFAULT_FONT_HEIGHT;

    ui->hot_id = 0;
}

void mt_ui_draw(MtUIRenderer *ui, MtCmdBuffer *cb)
{
    for (uint32_t i = 0; i < mt_array_size(ui->commands); ++i)
    {
        UICommand *cmd = &ui->commands[i];

        Mat4 transform = mat4_orthographic(
            0.0f,
            (float)cmd->state.viewport.width,
            0.0f,
            (float)cmd->state.viewport.height,
            0.0f,
            1.0f);

        mt_render.cmd_bind_pipeline(cb, ui->pipeline->pipeline);

        mt_render.cmd_bind_uniform(cb, &transform, sizeof(transform), 0, 0);

        mt_render.cmd_bind_image(cb, cmd->state.image, cmd->state.sampler, 0, 1);

        mt_render.cmd_bind_vertex_data(
            cb, cmd->vertices, mt_array_size(cmd->vertices) * sizeof(MtUIVertex));
        mt_render.cmd_bind_index_data(
            cb, cmd->indices, mt_array_size(cmd->indices) * sizeof(uint16_t), MT_INDEX_TYPE_UINT16);

        mt_render.cmd_draw_indexed(cb, mt_array_size(cmd->indices), 1, 0, 0, 0);
    }

    for (uint32_t i = 0; i < mt_array_size(ui->commands); ++i)
    {
        UICommand *cmd = &ui->commands[i];
        mt_array_free(ui->alloc, cmd->vertices);
        mt_array_free(ui->alloc, cmd->indices);
    }

    mt_array_set_size(ui->commands, 0);
}

static FontAtlas *get_atlas(MtFontAsset *asset, uint32_t height)
{
    FontAtlas *atlas = mt_hash_get_ptr(&asset->map, (uint64_t)height);
    if (!atlas)
    {
        MtAllocator *alloc = asset->asset_manager->alloc;

        // Create atlas
        mt_array_push(alloc, asset->atlases, (FontAtlas){0});
        atlas = mt_array_last(asset->atlases);
        memset(atlas, 0, sizeof(*atlas));

        stbtt_fontinfo stbfont;
        stbtt_InitFont(&stbfont, asset->font_data, 0);

        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(&stbfont, &ascent, &descent, &linegap);
        float scale   = stbtt_ScaleForMappingEmToPixels(&stbfont, (float)height);
        atlas->height = ((float)(ascent - descent + linegap) * scale + 0.5) * 0.5;

        int first_char = 0;
        int char_count = 255;

        atlas->dim      = 2048;
        uint8_t *pixels = mt_alloc(alloc, atlas->dim * atlas->dim);
        atlas->chardata = mt_alloc(alloc, sizeof(stbtt_bakedchar) * (char_count - first_char));

        stbtt_BakeFontBitmap(
            asset->font_data,
            0,
            (float)height,
            pixels,
            (int)atlas->dim,
            (int)atlas->dim,
            first_char,
            char_count,
            atlas->chardata);

        uint8_t *new_pixels = mt_alloc(alloc, 4 * atlas->dim * atlas->dim);
        for (uint32_t i = 0; i < atlas->dim * atlas->dim; ++i)
        {
            new_pixels[i * 4 + 0] = pixels[i];
            new_pixels[i * 4 + 1] = pixels[i];
            new_pixels[i * 4 + 2] = pixels[i];
            new_pixels[i * 4 + 3] = pixels[i];
        }

        mt_free(alloc, pixels);

        atlas->image = mt_render.create_image(
            asset->asset_manager->engine->device,
            &(MtImageCreateInfo){
                .width  = atlas->dim,
                .height = atlas->dim,
                .format = MT_FORMAT_RGBA8_UNORM,
            });

        mt_render.transfer_to_image(
            asset->asset_manager->engine->device,
            &(MtImageCopyView){.image = atlas->image},
            atlas->dim * atlas->dim * 4,
            new_pixels);

        mt_free(alloc, new_pixels);

        mt_hash_set_ptr(&asset->map, (uint64_t)height, atlas);
    }

    return atlas;
}

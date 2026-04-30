#include "display.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char* TAG = "display";

const char* persona_state_names[] = {
    "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"
};

static const char* BUDDY[] = {
    [P_SLEEP] =
        "            \n"
        "    .--.    \n"
        "  _( -- )_  \n"
        " (___zz___) \n"
        "  ~~~~~~~~  \n"
        "            ",
    [P_IDLE] =
        "            \n"
        "    .--.    \n"
        "  _( oo )_  \n"
        " (___..___) \n"
        "  ~~~~~~~~  \n"
        "            ",
    [P_BUSY] =
        "   !! !!    \n"
        "    .--.    \n"
        "  _( ** )_  \n"
        " (___!!___) \n"
        "  ~~~~~~~~  \n"
        "            ",
    [P_ATTENTION] =
        "   !! !!    \n"
        "    .--.    \n"
        "  _( OO )_  \n"
        " (___!!___) \n"
        "  !!!!!!!!  \n"
        "            ",
    [P_CELEBRATE] =
        "  \\o/ \\o/  \n"
        "   \\^.^/   \n"
        "    (>-<    \n"
        "   (> <)    \n"
        "            \n"
        "  *  *  *  ",
    [P_DIZZY] =
        "            \n"
        "    x-x     \n"
        "   (@.@)    \n"
        "    uuu     \n"
        "  ~~~~~~~   \n"
        "            ",
    [P_HEART] =
        "  <3  <3    \n"
        "   .--.     \n"
        "  _( **)_   \n"
        " (___<3__)  \n"
        "  ~~~~~~~   \n"
        "            ",
};

static struct {
    lv_obj_t* scr;
    lv_obj_t* header;
    lv_obj_t* conn_dot;
    lv_obj_t* status;
    lv_obj_t* buddy;
    lv_obj_t* session;
    lv_obj_t* msg;
    lv_obj_t* tokens;
    lv_obj_t* state_label;
    lv_obj_t* prompt;
    lv_obj_t* hint;
    enum PersonaState last_persona;
} ui;

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display with LVGL...");

    bsp_display_start();
    bsp_display_backlight_on();

    display_lock();

    ui.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui.scr, lv_color_hex(0x000000), LV_STATE_DEFAULT);

    ui.header = lv_label_create(ui.scr);
    lv_label_set_text(ui.header, "Claude Buddy Touch");
    lv_obj_set_style_text_color(ui.header, lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_align(ui.header, LV_ALIGN_TOP_LEFT, 10, 10);

    ui.conn_dot = lv_obj_create(ui.scr);
    lv_obj_remove_style_all(ui.conn_dot);
    lv_obj_set_size(ui.conn_dot, 10, 10);
    lv_obj_set_style_radius(ui.conn_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0xFF0000), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui.conn_dot, LV_OPA_COVER, 0);
    lv_obj_align(ui.conn_dot, LV_ALIGN_TOP_RIGHT, -10, 14);

    ui.status = lv_label_create(ui.scr);
    lv_label_set_text(ui.status, "Connecting...");
    lv_obj_set_style_text_color(ui.status, lv_color_hex(0xCCCCCC), LV_STATE_DEFAULT);
    lv_obj_align(ui.status, LV_ALIGN_TOP_LEFT, 10, 40);

    ui.buddy = lv_label_create(ui.scr);
    lv_obj_set_style_text_color(ui.buddy, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui.buddy, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui.buddy, &lv_font_montserrat_36, LV_STATE_DEFAULT);
    lv_obj_set_width(ui.buddy, DISP_W);
    lv_obj_align(ui.buddy, LV_ALIGN_TOP_MID, 0, 80);

    ui.session = lv_label_create(ui.scr);
    lv_obj_set_style_text_color(ui.session, lv_color_hex(0xAAAAAA), LV_STATE_DEFAULT);
    lv_obj_align(ui.session, LV_ALIGN_TOP_LEFT, 10, 340);

    ui.msg = lv_label_create(ui.scr);
    lv_obj_set_width(ui.msg, DISP_W - 20);
    lv_obj_set_style_text_color(ui.msg, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui.msg, LV_TEXT_ALIGN_LEFT, LV_STATE_DEFAULT);
    lv_obj_align(ui.msg, LV_ALIGN_TOP_LEFT, 10, 370);

    ui.tokens = lv_label_create(ui.scr);
    lv_obj_set_style_text_color(ui.tokens, lv_color_hex(0x888888), LV_STATE_DEFAULT);
    lv_obj_align(ui.tokens, LV_ALIGN_TOP_LEFT, 10, 410);

    ui.state_label = lv_label_create(ui.scr);
    lv_obj_set_style_text_color(ui.state_label, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
    lv_obj_align(ui.state_label, LV_ALIGN_TOP_LEFT, 10, 440);

    ui.prompt = lv_label_create(ui.scr);
    lv_obj_set_width(ui.prompt, DISP_W - 20);
    lv_obj_set_style_text_color(ui.prompt, lv_color_hex(0xFFCC00), LV_STATE_DEFAULT);
    lv_obj_align(ui.prompt, LV_ALIGN_TOP_LEFT, 10, 480);

    ui.hint = lv_label_create(ui.scr);
    lv_label_set_text(ui.hint, "Tap: next  Hold: toggle demo");
    lv_obj_set_style_text_color(ui.hint, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_align(ui.hint, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    lv_scr_load(ui.scr);

    ui.last_persona = P_COUNT;

    display_unlock();

    ESP_LOGI(TAG, "Display initialized: %dx%d", DISP_W, DISP_H);
}

void display_lock(void)
{
    bsp_display_lock(-1);
}

void display_unlock(void)
{
    bsp_display_unlock();
}

lv_disp_t* display_get(void)
{
    return lv_disp_get_default();
}

void display_show_hello(void)
{
    display_lock();

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_STATE_DEFAULT);

    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "Claude Buddy Touch\n\nConnecting...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_center(label);

    lv_scr_load(scr);

    display_unlock();
}

void display_update(const struct TamaState* state, enum PersonaState persona)
{
    if (!ui.scr) return;

    char buf[128];

    if (state->connected) {
        if (state->promptId[0]) {
            lv_label_set_text(ui.status, "Prompt waiting!");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0xFFCC00), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0xFFCC00), LV_STATE_DEFAULT);
        } else if (state->sessionsRunning > 0 || state->sessionsWaiting > 0) {
            lv_label_set_text(ui.status, "Active");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
        } else if (state->sessionsTotal > 0) {
            lv_label_set_text(ui.status, "Idle");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x88FF88), LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(ui.status, "Connected");
            lv_obj_set_style_text_color(ui.status, lv_color_hex(0x8888FF), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0x8888FF), LV_STATE_DEFAULT);
        }
    } else {
        lv_label_set_text(ui.status, "Connecting...");
        lv_obj_set_style_text_color(ui.status, lv_color_hex(0xCC6666), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui.conn_dot, lv_color_hex(0xFF0000), LV_STATE_DEFAULT);
    }

    if (persona != ui.last_persona) {
        if (persona >= 0 && persona < P_COUNT && BUDDY[persona]) {
            lv_label_set_text(ui.buddy, BUDDY[persona]);
        }
        ui.last_persona = persona;
    }

    snprintf(buf, sizeof(buf), "Sessions: %u total  %u running  %u waiting",
             state->sessionsTotal, state->sessionsRunning, state->sessionsWaiting);
    lv_label_set_text(ui.session, buf);

    lv_label_set_text(ui.msg, state->msg[0] ? state->msg : "(no message)");

    snprintf(buf, sizeof(buf), "Tokens today: %lu", (unsigned long)state->tokensToday);
    lv_label_set_text(ui.tokens, buf);

    snprintf(buf, sizeof(buf), "State: %s", persona_state_names[persona]);
    lv_label_set_text(ui.state_label, buf);

    if (state->promptId[0]) {
        snprintf(buf, sizeof(buf), "Prompt: %s\nTool: %s\n%s",
                 state->promptId, state->promptTool, state->promptHint);
        lv_label_set_text(ui.prompt, buf);
        lv_obj_clear_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.prompt, LV_OBJ_FLAG_HIDDEN);
    }
}

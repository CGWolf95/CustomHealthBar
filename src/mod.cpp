#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/ui.h"
#include "mods/svc/log.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_meter_HIO.h"
#include "d/d_meter2.h"
#include "d/d_meter2_draw.h"
#include "m_Do/m_Do_graphic.h"
#include "d/d_pane_class.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

DEFINE_MOD();

IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);

namespace {

constexpr int64_t kAlignmentLeft = 0;
constexpr int64_t kAlignmentCenter = 1;
constexpr int64_t kAlignmentRight = 2;

constexpr int64_t kHorizontalAnchorLeft = 0;
constexpr int64_t kHorizontalAnchorCenter = 1;
constexpr int64_t kHorizontalAnchorRight = 2;
constexpr int64_t kVerticalAnchorTop = 0;
constexpr int64_t kVerticalAnchorCenter = 1;
constexpr int64_t kVerticalAnchorBottom = 2;

constexpr f32 kAutoVisibilityFadeFrames = 10.0f;
constexpr f32 kFramesPerSecond = 60.0f;

ConfigVarHandle g_xPosVar = 0;
ConfigVarHandle g_yPosVar = 0;
ConfigVarHandle g_alignmentVar = 0;
ConfigVarHandle g_hAnchorVar = 0;
ConfigVarHandle g_vAnchorVar = 0;
ConfigVarHandle g_lengthVar = 0;
ConfigVarHandle g_scaleVar = 0;
ConfigVarHandle g_healthUpdateSpeedVar = 0;
ConfigVarHandle g_colorVar = 0;
ConfigVarHandle g_autoVisibilityVar = 0;
ConfigVarHandle g_visibilityTimerVar = 0;
UiStyleHandle g_settingsStyle = 0;

f32 g_displayRatio = -1.0f;
f32 g_healthBarAlpha = 0.0f;
bool g_drawingCustomBar = false;
CPaneMgr* g_colorOverridePane = nullptr;
f32 g_autoVisibilityAlpha = 1.0f;
int64_t g_autoVisibilityRemainingFrames = 0;
u16 g_previousLife = 0;
bool g_previousLifeInitialized = false;
bool g_autoVisibilityWasEnabled = false;
dMeter2Draw_c* g_anchorMeter = nullptr;
int64_t g_horizontalAnchor = kHorizontalAnchorLeft;
int64_t g_verticalAnchor = kVerticalAnchorTop;
f32 g_anchorXOffset = 0.0f;
f32 g_anchorYOffset = 0.0f;
JUtility::TColor g_colorOverrideBlack(0, 0, 0, 255);
JUtility::TColor g_colorOverrideWhite(255, 77, 77, 255);

DEFINE_HOOK(&dMeter2Draw_c::draw, HealthBarDrawHook);
DEFINE_HOOK(&dMeter2Draw_c::drawKanteraScreen, LanternScreenHook);
DEFINE_HOOK(&dMeter2_c::presentAnims, HealthVisibilityHook);
DEFINE_HOOK(&CPaneMgr::setBlackWhite, PaneColorHook);
DEFINE_HOOK(&CPaneMgr::paneTrans, HealthAnchorHook);

JUtility::TColor parse_color(const char* text) {
    unsigned int r = 255;
    unsigned int g = 77;
    unsigned int b = 77;

    if (text != nullptr) {
        const char* value = text;
        if (*value == '#') {
            ++value;
        }

        unsigned int parsedR = 0;
        unsigned int parsedG = 0;
        unsigned int parsedB = 0;
        if (std::sscanf(value, "%2x%2x%2x", &parsedR, &parsedG, &parsedB) == 3) {
            r = std::min(parsedR, 255u);
            g = std::min(parsedG, 255u);
            b = std::min(parsedB, 255u);
        }
    }

    return JUtility::TColor(static_cast<u8>(r), static_cast<u8>(g), static_cast<u8>(b), 255);
}

void update_color_override() {
    char colorText[32] = {};
    if (svc_config->get_string(mod_ctx, g_colorVar, colorText, sizeof(colorText), nullptr) != MOD_OK) {
        g_colorOverrideWhite = JUtility::TColor(255, 77, 77, 255);
    } else {
        g_colorOverrideWhite = parse_color(colorText);
    }

    g_colorOverrideBlack = JUtility::TColor(
        static_cast<u8>(g_colorOverrideWhite.r * 0.25f),
        static_cast<u8>(g_colorOverrideWhite.g * 0.25f),
        static_cast<u8>(g_colorOverrideWhite.b * 0.25f),
        255);
}

static HookAction on_pane_set_black_white_pre(ModContext*, void* args, void*, void*) {
    if (!g_drawingCustomBar || g_colorOverridePane == nullptr) {
        return HOOK_CONTINUE;
    }

    CPaneMgr* pane = mods::arg<CPaneMgr*>(args, 0);
    if (pane != g_colorOverridePane) {
        return HOOK_CONTINUE;
    }

    auto& black = mods::arg_ref<JUtility::TColor>(args, 1);
    auto& white = mods::arg_ref<JUtility::TColor>(args, 2);
    black = g_colorOverrideBlack;
    white = g_colorOverrideWhite;
    return HOOK_CONTINUE;
}

f32 get_target_ratio() {
    const u16 maxHealth = dComIfGs_getMaxLifeGauge();
    const u16 life = dComIfGs_getLife();
    if (maxHealth == 0) {
        return 0.0f;
    }

    return std::clamp(static_cast<f32>(life) / static_cast<f32>(maxHealth), 0.0f, 1.0f);
}

static HookAction on_pane_trans_pre(ModContext*, void* args, void*, void*) {
    if (!g_drawingCustomBar || g_anchorMeter == nullptr || g_anchorMeter->mpMagicParent == nullptr) {
        return HOOK_CONTINUE;
    }

    CPaneMgr* pane = mods::arg<CPaneMgr*>(args, 0);
    if (pane != g_anchorMeter->mpMagicParent) {
        return HOOK_CONTINUE;
    }

    const f32 scaleX = pane->getScaleX();
    const f32 scaleY = pane->getScaleY();
    const f32 parentSizeX = pane->getSizeX();
    const f32 parentSizeY = pane->getSizeY();
    const f32 parentCenterX = pane->getInitCenterPosX();
    const f32 parentCenterY = pane->getInitCenterPosY();

    const f32 visualLeft = g_anchorMeter->mpMagicFrameL->getPosX();
    const f32 visualTop = g_anchorMeter->mpMagicFrameL->getPosY();
    const f32 visualRight =
        g_anchorMeter->mpMagicFrameR->getPosX() + g_anchorMeter->mpMagicFrameR->getSizeX();
    const f32 visualBottom =
        g_anchorMeter->mpMagicFrameL->getPosY() + g_anchorMeter->mpMagicFrameL->getSizeY();
    const f32 visualWidth = visualRight - visualLeft;
    const f32 visualHeight = visualBottom - visualTop;

    const f32 minX = mDoGph_gInf_c::getMinXF();
    const f32 maxX = mDoGph_gInf_c::getMaxXF();
    const f32 minY = mDoGph_gInf_c::getMinYF();
    const f32 maxY = mDoGph_gInf_c::getMaxYF();
    const f32 viewportCenterX = (minX + maxX) * 0.5f;
    const f32 viewportCenterY = (minY + maxY) * 0.5f;

    f32 targetLeft = minX;
    switch (g_horizontalAnchor) {
    case kHorizontalAnchorCenter:
        targetLeft = viewportCenterX - visualWidth * scaleX * 0.5f;
        break;
    case kHorizontalAnchorRight:
        targetLeft = maxX - visualWidth * scaleX;
        break;
    case kHorizontalAnchorLeft:
    default:
        break;
    }
    targetLeft += g_anchorXOffset;

    f32 targetTop = minY;
    switch (g_verticalAnchor) {
    case kVerticalAnchorCenter:
        targetTop = viewportCenterY - visualHeight * scaleY * 0.5f;
        break;
    case kVerticalAnchorBottom:
        targetTop = maxY - visualHeight * scaleY;
        break;
    case kVerticalAnchorTop:
    default:
        break;
    }
    targetTop += g_anchorYOffset;

    // Match CPaneMgr::paneTrans() exactly, but supply the center point that puts the
    // actual Lantern frame edge on the requested viewport edge/center. At this hook
    // point drawKanteraScreen() has already applied Dusklight's private HUD scale and
    // has already moved/resized the frame for the configured Length.
    auto& paneTransX = mods::arg_ref<f32>(args, 1);
    auto& paneTransY = mods::arg_ref<f32>(args, 2);
    paneTransX =
        targetLeft - visualLeft * scaleX - parentCenterX + parentSizeX * 0.5f;
    paneTransY =
        targetTop - visualTop * scaleY - parentCenterY + parentSizeY * 0.5f;

    return HOOK_CONTINUE;
}

void update_auto_visibility(f32 hudAlpha) {
    bool enabled = false;
    int64_t timerSeconds = 10;
    if (svc_config->get_bool(mod_ctx, g_autoVisibilityVar, &enabled) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_visibilityTimerVar, &timerSeconds) != MOD_OK) {
        enabled = false;
        timerSeconds = 5;
    }

    timerSeconds = std::clamp<int64_t>(timerSeconds, 1, 60);
    const u16 life = dComIfGs_getLife();

    if (!enabled) {
        g_autoVisibilityAlpha = 1.0f;
        g_autoVisibilityRemainingFrames = 0;
        g_previousLife = life;
        g_previousLifeInitialized = true;
        g_autoVisibilityWasEnabled = false;
        return;
    }

    if (!g_autoVisibilityWasEnabled) {
        g_previousLife = life;
        g_previousLifeInitialized = true;
        g_autoVisibilityRemainingFrames = 0;
        g_autoVisibilityAlpha = 0.0f;
        g_autoVisibilityWasEnabled = true;
        return;
    }

    if (!g_previousLifeInitialized) {
        g_previousLife = life;
        g_previousLifeInitialized = true;
    } else if (life != g_previousLife) {
        g_previousLife = life;
        g_autoVisibilityRemainingFrames = timerSeconds * static_cast<int64_t>(kFramesPerSecond);
    }

    if (hudAlpha > 0.001f && g_autoVisibilityRemainingFrames > 0) {
        --g_autoVisibilityRemainingFrames;
    }

    f32 targetAlpha = 0.0f;
    if (g_autoVisibilityRemainingFrames > 0) {
        if (g_autoVisibilityRemainingFrames < static_cast<int64_t>(kAutoVisibilityFadeFrames)) {
            targetAlpha = static_cast<f32>(g_autoVisibilityRemainingFrames) / kAutoVisibilityFadeFrames;
        } else {
            targetAlpha = 1.0f;
        }
    }

    g_autoVisibilityAlpha += (targetAlpha - g_autoVisibilityAlpha) * 0.35f;
    if (std::fabs(targetAlpha - g_autoVisibilityAlpha) < 0.001f) {
        g_autoVisibilityAlpha = targetAlpha;
    }
}

void prepare_health_bar_meter(dMeter2Draw_c* meter, f32 ratio, f32 lengthScale, f32 posX, f32 posY) {
    const f32 frameLeftX = meter->mpMagicFrameL->getInitPosX();
    const f32 frameSpan = meter->mpMagicFrameR->getInitPosX() - frameLeftX;
    const f32 meterWidth = meter->mpMagicMeter->getInitSizeX();
    const f32 lengthGrowth = meterWidth * (lengthScale - 1.0f);

    meter->field_0x584[1] = ratio * meterWidth * lengthScale;
    meter->field_0x590[1] = meter->mpMagicMeter->getInitSizeY();
    meter->field_0x59c[1] = frameLeftX + frameSpan + lengthGrowth;
    meter->field_0x5a8[1] = meter->mpMagicFrameL->getInitPosY();
    meter->field_0x5b4[1] = meter->mpMagicBase->getInitSizeX() + lengthGrowth;
    meter->field_0x5c0[1] = meter->mpMagicBase->getInitSizeY();
    meter->field_0x5cc[1] = g_drawHIO.mLanternMeterScale;
    meter->field_0x5d8[1] = g_drawHIO.mLanternMeterScale;
    meter->field_0x5e4[1] = posX;
    meter->field_0x5f0[1] = posY;
}

void draw_health_bar(dMeter2Draw_c* meter) {
    if (meter == nullptr || meter->mpKanteraScreen == nullptr || meter->mpMagicParent == nullptr ||
        meter->mpMagicBase == nullptr || meter->mpMagicFrameL == nullptr ||
        meter->mpMagicFrameR == nullptr || meter->mpMagicMeter == nullptr || g_healthBarAlpha <= 0.001f) {
        return;
    }

    int64_t xPos = 0;
    int64_t yPos = 0;
    int64_t alignment = kAlignmentLeft;
    int64_t hAnchor = kHorizontalAnchorLeft;
    int64_t vAnchor = kVerticalAnchorTop;
    int64_t lengthPercent = 200;
    int64_t scalePercent = 100;
    int64_t healthUpdateSpeed = 5;
    if (svc_config->get_int(mod_ctx, g_xPosVar, &xPos) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_yPosVar, &yPos) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_alignmentVar, &alignment) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_hAnchorVar, &hAnchor) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_vAnchorVar, &vAnchor) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_lengthVar, &lengthPercent) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_scaleVar, &scalePercent) != MOD_OK ||
        svc_config->get_int(mod_ctx, g_healthUpdateSpeedVar, &healthUpdateSpeed) != MOD_OK) {
        return;
    }

    const f32 targetRatio = get_target_ratio();
    if (g_displayRatio < 0.0f) {
        g_displayRatio = targetRatio;
    } else {
        const f32 delta = targetRatio - g_displayRatio;
        const f32 updateFactor = std::clamp(static_cast<f32>(healthUpdateSpeed) / 100.0f, 0.01f, 1.0f);
        g_displayRatio += delta * updateFactor;
        if (std::fabs(delta) < 0.001f) {
            g_displayRatio = targetRatio;
        }
    }
    g_displayRatio = std::clamp(g_displayRatio, 0.0f, 1.0f);

    const f32 lengthScale = std::clamp(static_cast<f32>(lengthPercent) / 100.0f, 0.5f, 3.0f);
    const f32 customScale = std::clamp(static_cast<f32>(scalePercent) / 100.0f, 0.5f, 2.0f);

    prepare_health_bar_meter(
        meter, g_displayRatio, lengthScale, static_cast<f32>(xPos), static_cast<f32>(yPos));
    meter->field_0x5cc[1] = g_drawHIO.mLanternMeterScale * customScale;
    meter->field_0x5d8[1] = g_drawHIO.mLanternMeterScale * customScale;

    const f32 savedMeterAlphaRate = meter->mMeterAlphaRate[1];
    meter->mMeterAlphaRate[1] = g_healthBarAlpha * g_autoVisibilityAlpha;
    update_color_override();
    g_colorOverridePane = meter->mpMagicMeter;
    g_anchorMeter = meter;
    g_horizontalAnchor = hAnchor;
    g_verticalAnchor = vAnchor;
    g_anchorXOffset = static_cast<f32>(xPos);
    g_anchorYOffset = static_cast<f32>(yPos);
    g_drawingCustomBar = true;
    meter->drawKanteraScreen(1);
    g_drawingCustomBar = false;
    g_anchorMeter = nullptr;
    g_colorOverridePane = nullptr;
    meter->mMeterAlphaRate[1] = savedMeterAlphaRate;

    const f32 totalWidth = meter->mpMagicMeter->getInitSizeX() * lengthScale;
    const f32 initialX = meter->mpMagicMeter->getInitPosX();
    const f32 currentWidth = meter->mpMagicMeter->getSizeX();
    if (totalWidth <= 0.0f) {
        return;
    }

    f32 offset = 0.0f;
    if (alignment == kAlignmentCenter) {
        offset = (totalWidth - currentWidth) * 0.5f;
    } else if (alignment == kAlignmentRight) {
        offset = totalWidth - currentWidth;
    }

    meter->mpMagicMeter->move(initialX + offset, meter->mpMagicMeter->getInitPosY());
}

static HookAction on_lantern_screen_pre(ModContext*, void* args, void*, void*) {
    const u8 meterType = mods::arg<u8>(args, 1);
    if (meterType == 1 && !g_drawingCustomBar) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

static void on_health_visibility_post(ModContext*, void* args, void*, void*) {
    dMeter2_c* meter = mods::arg<dMeter2_c*>(args, 0);
    dMeter2Draw_c* draw = meter != nullptr ? meter->getMeterDrawPtr() : nullptr;
    if (draw == nullptr || draw->mpLifeParent == nullptr) {
        g_healthBarAlpha = 0.0f;
        return;
    }

    // presentAnims() has just applied Dusklight's own HUD fade state. Reuse that exact
    // alpha for the replacement bar, then hide only the vanilla heart panes.
    g_healthBarAlpha = draw->mpLifeParent->getAlphaRate();
    update_auto_visibility(g_healthBarAlpha);
    draw->mpLifeParent->setAlphaRate(0.0f);

    if (draw->mpBigHeart != nullptr) {
        draw->mpBigHeart->setAlphaRate(0.0f);
    }
}

static void on_health_bar_draw_post(ModContext*, void* args, void*, void*) {
    dMeter2Draw_c* meter = mods::arg<dMeter2Draw_c*>(args, 0);
    draw_health_bar(meter);
}

static void set_config_int(ConfigVarHandle var, ModContext* ctx, int64_t value) {
    svc_config->set_int(ctx, var, value);
}

static bool anchor_is_selected(ModContext* ctx, void* user_data, ConfigVarHandle var) {
    const auto* value = static_cast<const int64_t*>(user_data);
    int64_t current = -1;
    if (value == nullptr || svc_config->get_int(ctx, var, &current) != MOD_OK) {
        return false;
    }
    return current == *value;
}

static bool horizontal_anchor_is_selected(ModContext* ctx, void* user_data) {
    return anchor_is_selected(ctx, user_data, g_hAnchorVar);
}

static bool vertical_anchor_is_selected(ModContext* ctx, void* user_data) {
    return anchor_is_selected(ctx, user_data, g_vAnchorVar);
}

static void horizontal_anchor_pressed(ModContext* ctx, void* user_data) {
    const auto* value = static_cast<const int64_t*>(user_data);
    if (value != nullptr) {
        set_config_int(g_hAnchorVar, ctx, *value);
    }
}

static void vertical_anchor_pressed(ModContext* ctx, void* user_data) {
    const auto* value = static_cast<const int64_t*>(user_data);
    if (value != nullptr) {
        set_config_int(g_vAnchorVar, ctx, *value);
    }
}

static bool alignment_is_selected(ModContext* ctx, void* user_data) {
    const auto* value = static_cast<const int64_t*>(user_data);
    int64_t current = kAlignmentLeft;
    if (value == nullptr || svc_config->get_int(ctx, g_alignmentVar, &current) != MOD_OK) {
        return false;
    }
    return current == *value;
}

static void alignment_pressed(ModContext* ctx, void* user_data) {
    const auto* value = static_cast<const int64_t*>(user_data);
    if (value != nullptr) {
        set_config_int(g_alignmentVar, ctx, *value);
    }
}

static bool auto_visibility_timer_disabled(ModContext* ctx, void*) {
    bool enabled = false;
    return svc_config->get_bool(ctx, g_autoVisibilityVar, &enabled) != MOD_OK || !enabled;
}

ModResult add_number_control(
    ModContext* ctx, UiElementHandle pane, const char* label, ConfigVarHandle var, const char* help) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = var;
    control.min = -2048;
    control.max = 2048;
    control.step = 1;
    return svc_ui->pane_add_control(ctx, pane, &control, nullptr);
}

ModResult add_choice_row_style(ModContext* ctx, UiElementHandle row) {
    return svc_ui->elem_set_class(ctx, row, "health-bar-choice-row", true);
}

ModResult build_choice_row(
    ModContext* ctx,
    UiElementHandle pane,
    const char* const* labels,
    const int64_t* values,
    size_t count,
    UiPressedFn pressed,
    UiPredicateFn selected) {
    UiRowDesc rowDesc = UI_ROW_DESC_INIT;
    rowDesc.align = UI_ROW_ALIGN_START;
    UiElementHandle row = 0;
    ModResult result = svc_ui->pane_add_row(ctx, pane, &rowDesc, &row);
    if (result != MOD_OK) {
        return result;
    }

    result = add_choice_row_style(ctx, row);
    if (result != MOD_OK) {
        return result;
    }

    for (size_t i = 0; i < count; ++i) {
        UiControlDesc button = UI_CONTROL_DESC_INIT;
        button.kind = UI_CONTROL_BUTTON;
        button.label = labels[i];
        button.on_pressed = pressed;
        button.is_selected = selected;
        button.user_data = const_cast<int64_t*>(&values[i]);
        result = svc_ui->pane_add_control(ctx, row, &button, nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    return MOD_OK;
}

ModResult build_settings(ModContext* ctx, UiElementHandle pane, void*, ModError*) {
    ModResult result = svc_ui->pane_add_section(ctx, pane, "CUSTOMIZE");
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc color = UI_CONTROL_DESC_INIT;
    color.kind = UI_CONTROL_COLOR;
    color.label = "Color";
    color.help_rml = "Color applied to the health bar fill.";
    color.binding = UI_BINDING_CONFIG_VAR;
    color.config_var = g_colorVar;
    color.color_alpha = false;
    result = svc_ui->pane_add_control(ctx, pane, &color, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    result = add_number_control(ctx, pane, "X Position", g_xPosVar,
                                "Horizontal offset from the selected horizontal anchor.");
    if (result != MOD_OK) {
        return result;
    }

    result = add_number_control(ctx, pane, "Y Position", g_yPosVar,
                                "Vertical offset from the selected vertical anchor.");
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc length = UI_CONTROL_DESC_INIT;
    length.kind = UI_CONTROL_NUMBER;
    length.label = "Length";
    length.help_rml = "Changes the health bar length. 100% is the original full Lantern-meter length.";
    length.binding = UI_BINDING_CONFIG_VAR;
    length.config_var = g_lengthVar;
    length.min = 50;
    length.max = 300;
    length.step = 1;
    length.suffix = "%";
    result = svc_ui->pane_add_control(ctx, pane, &length, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc scale = UI_CONTROL_DESC_INIT;
    scale.kind = UI_CONTROL_NUMBER;
    scale.label = "Scale";
    scale.help_rml = "Changes the overall size of the health bar while preserving its length and decorative end caps.";
    scale.binding = UI_BINDING_CONFIG_VAR;
    scale.config_var = g_scaleVar;
    scale.min = 50;
    scale.max = 200;
    scale.step = 1;
    scale.suffix = "%";
    result = svc_ui->pane_add_control(ctx, pane, &scale, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc updateSpeed = UI_CONTROL_DESC_INIT;
    updateSpeed.kind = UI_CONTROL_NUMBER;
    updateSpeed.label = "Health Update Speed";
    updateSpeed.help_rml = "Controls how quickly the displayed health catches up to current HP. Lower values are slower; 100% is immediate.";
    updateSpeed.binding = UI_BINDING_CONFIG_VAR;
    updateSpeed.config_var = g_healthUpdateSpeedVar;
    updateSpeed.min = 1;
    updateSpeed.max = 100;
    updateSpeed.step = 1;
    updateSpeed.suffix = "%";
    result = svc_ui->pane_add_control(ctx, pane, &updateSpeed, nullptr);
    if (result != MOD_OK) {
        return result;
    }

    result = svc_ui->pane_add_section(ctx, pane, "FILL ALIGNMENT");
    if (result != MOD_OK) {
        return result;
    }

    static const char* const alignmentLabels[] = {"Left", "Center", "Right"};
    static const int64_t alignmentValues[] = {kAlignmentLeft, kAlignmentCenter, kAlignmentRight};
    result = build_choice_row(ctx, pane, alignmentLabels, alignmentValues, 3,
                              alignment_pressed, alignment_is_selected);
    if (result != MOD_OK) {
        return result;
    }

    result = svc_ui->pane_add_section(ctx, pane, "HORIZONTAL ANCHOR");
    if (result != MOD_OK) {
        return result;
    }

    static const char* const hLabels[] = {"Left", "Center", "Right"};
    static const int64_t hValues[] = {kHorizontalAnchorLeft, kHorizontalAnchorCenter, kHorizontalAnchorRight};
    result = build_choice_row(ctx, pane, hLabels, hValues, 3,
                              horizontal_anchor_pressed, horizontal_anchor_is_selected);
    if (result != MOD_OK) {
        return result;
    }

    result = svc_ui->pane_add_section(ctx, pane, "VERTICAL ANCHOR");
    if (result != MOD_OK) {
        return result;
    }

    static const char* const vLabels[] = {"Top", "Center", "Bottom"};
    static const int64_t vValues[] = {kVerticalAnchorTop, kVerticalAnchorCenter, kVerticalAnchorBottom};
    result = build_choice_row(ctx, pane, vLabels, vValues, 3,
                              vertical_anchor_pressed, vertical_anchor_is_selected);
    if (result != MOD_OK) {
        return result;
    }

    result = svc_ui->pane_add_section(ctx, pane, "AUTO VISIBILITY");
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc autoVisibility = UI_CONTROL_DESC_INIT;
    autoVisibility.kind = UI_CONTROL_TOGGLE;
    autoVisibility.label = "Auto Visibility";
    autoVisibility.help_rml = "Hide the health bar until Link's current HP changes, then show it for the configured duration.";
    autoVisibility.binding = UI_BINDING_CONFIG_VAR;
    autoVisibility.config_var = g_autoVisibilityVar;
    UiElementHandle autoVisibilityElement = 0;
    result = svc_ui->pane_add_control(ctx, pane, &autoVisibility, &autoVisibilityElement);
    if (result != MOD_OK) {
        return result;
    }
    result = svc_ui->elem_set_class(ctx, autoVisibilityElement, "health-bar-wide-label", true);
    if (result != MOD_OK) {
        return result;
    }

    UiControlDesc visibilityTimer = UI_CONTROL_DESC_INIT;
    visibilityTimer.kind = UI_CONTROL_NUMBER;
    visibilityTimer.label = "Visibility Timer";
    visibilityTimer.help_rml = "How long the health bar remains visible after HP changes.";
    visibilityTimer.binding = UI_BINDING_CONFIG_VAR;
    visibilityTimer.config_var = g_visibilityTimerVar;
    visibilityTimer.min = 1;
    visibilityTimer.max = 60;
    visibilityTimer.step = 1;
    visibilityTimer.suffix = " seconds";
    visibilityTimer.is_disabled = auto_visibility_timer_disabled;
    UiElementHandle visibilityTimerElement = 0;
    result = svc_ui->pane_add_control(ctx, pane, &visibilityTimer, &visibilityTimerElement);
    if (result != MOD_OK) {
        return result;
    }
    return svc_ui->elem_set_class(ctx, visibilityTimerElement, "health-bar-wide-label", true);
}

ModResult register_int_config(
    ModContext* ctx, const char* name, int64_t defaultValue, ConfigVarHandle* outVar) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_INT;
    desc.default_int = defaultValue;
    return svc_config->register_var(ctx, &desc, outVar);
}

ModResult register_bool_config(
    ModContext* ctx, const char* name, bool defaultValue, ConfigVarHandle* outVar) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = defaultValue;
    return svc_config->register_var(ctx, &desc, outVar);
}

ModResult register_string_config(
    ModContext* ctx, const char* name, const char* defaultValue, ConfigVarHandle* outVar) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_STRING;
    desc.default_string = defaultValue;
    return svc_config->register_var(ctx, &desc, outVar);
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = register_int_config(mod_ctx, "x_position", 0, &g_xPosVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register x_position");
    }

    result = register_int_config(mod_ctx, "y_position", 0, &g_yPosVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register y_position");
    }

    result = register_int_config(mod_ctx, "alignment", kAlignmentLeft, &g_alignmentVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register alignment");
    }

    result = register_int_config(mod_ctx, "horizontal_anchor", kHorizontalAnchorLeft, &g_hAnchorVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register horizontal_anchor");
    }

    result = register_int_config(mod_ctx, "vertical_anchor", kVerticalAnchorTop, &g_vAnchorVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register vertical_anchor");
    }

    result = register_int_config(mod_ctx, "length_percent", 200, &g_lengthVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register length_percent");
    }

    result = register_int_config(mod_ctx, "scale_percent", 100, &g_scaleVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register scale_percent");
    }

    result = register_int_config(mod_ctx, "health_update_speed", 5, &g_healthUpdateSpeedVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register health_update_speed");
    }

    result = register_string_config(mod_ctx, "color", "#FF4D4D", &g_colorVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register color");
    }

    result = register_bool_config(mod_ctx, "auto_visibility", false, &g_autoVisibilityVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register auto_visibility");
    }

    result = register_int_config(mod_ctx, "visibility_timer", 10, &g_visibilityTimerVar);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register visibility_timer");
    }

    static const char* const kSettingsStyle = R"RCSS(
ui-row.health-bar-choice-row > * {
    flex: 1 1 0;
    min-width: 0;
}

select-button.health-bar-wide-label key {
    flex: 1 1 0;
}

select-button.health-bar-wide-label value {
    flex: 0 0 auto;
}
)RCSS";

    result = svc_ui->register_styles(mod_ctx, UI_SCOPE_WINDOW, kSettingsStyle, &g_settingsStyle);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register settings row style");
    }

    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_settings;
    result = svc_ui->register_mods_panel(mod_ctx, &panel);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register health bar settings panel");
    }

    result = mods::hook::add_pre<PaneColorHook>(on_pane_set_black_white_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to install pane color hook");
    }

    result = mods::hook::add_pre<HealthAnchorHook>(on_pane_trans_pre);
    if (result != MOD_OK) {
        mods::hook::uninstall<PaneColorHook>();
        return mods::set_error(error, result, "failed to install health anchor hook");
    }

    result = mods::hook::add_pre<LanternScreenHook>(on_lantern_screen_pre);
    if (result != MOD_OK) {
        mods::hook::uninstall<HealthAnchorHook>();
        mods::hook::uninstall<PaneColorHook>();
        return mods::set_error(error, result, "failed to install Lantern-screen hook");
    }

    result = mods::hook::add_post<HealthVisibilityHook>(on_health_visibility_post);
    if (result != MOD_OK) {
        mods::hook::uninstall<LanternScreenHook>();
        mods::hook::uninstall<HealthAnchorHook>();
        mods::hook::uninstall<PaneColorHook>();
        return mods::set_error(error, result, "failed to install HUD visibility hook");
    }

    result = mods::hook::add_post<HealthBarDrawHook>(on_health_bar_draw_post);
    if (result != MOD_OK) {
        mods::hook::uninstall<HealthVisibilityHook>();
        mods::hook::uninstall<LanternScreenHook>();
        mods::hook::uninstall<HealthAnchorHook>();
        mods::hook::uninstall<PaneColorHook>();
        return mods::set_error(error, result, "failed to install health-bar draw hook");
    }

    mods::log::info("Custom Health Bar initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    if (g_settingsStyle != 0) {
        svc_ui->unregister_styles(mod_ctx, g_settingsStyle);
        g_settingsStyle = 0;
    }

    const ModResult drawHookResult = mods::hook::uninstall<HealthBarDrawHook>();
    const ModResult visibilityHookResult = mods::hook::uninstall<HealthVisibilityHook>();
    const ModResult lanternHookResult = mods::hook::uninstall<LanternScreenHook>();
    const ModResult anchorHookResult = mods::hook::uninstall<HealthAnchorHook>();
    const ModResult colorHookResult = mods::hook::uninstall<PaneColorHook>();
    g_displayRatio = -1.0f;
    g_healthBarAlpha = 0.0f;
    g_drawingCustomBar = false;
    g_anchorMeter = nullptr;
    g_colorOverridePane = nullptr;
    g_autoVisibilityAlpha = 1.0f;
    g_autoVisibilityRemainingFrames = 0;
    g_previousLife = 0;
    g_previousLifeInitialized = false;
    g_autoVisibilityWasEnabled = false;
    return drawHookResult != MOD_OK ? drawHookResult
         : (visibilityHookResult != MOD_OK ? visibilityHookResult
         : (lanternHookResult != MOD_OK ? lanternHookResult
         : (anchorHookResult != MOD_OK ? anchorHookResult : colorHookResult)));
}

}  // extern "C"

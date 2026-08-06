#include "meshcompromise/bridge_ui.h"

#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "meshcompromise/airtime.h"
#include "meshcompromise/bridge_thread.h"
#include "meshcompromise/mirror_module.h"
#include "meshcompromise/ui_input.h"

namespace meshcompromise
{

namespace
{

constexpr size_t kBodyRows = 6;
constexpr uint32_t kUiFocusGraceMs = 4000;

} // namespace

BridgeUiModule *bridgeUiModule = nullptr;

BridgeUiModule::BridgeUiModule() : MeshModule("MeshCompromiseUI")
{
    if (inputBroker != nullptr)
        inputObserver.observe(inputBroker);
    else
        LOG_WARN("MeshCompromise UI found no input broker, settings are not reachable");
    if (bridgeThread != nullptr)
        draft_ = bridgeThread->settings();
    LOG_INFO("MeshCompromise UI up");
}

bool BridgeUiModule::wantUIFrame()
{
    return bridgeThread != nullptr;
}

void BridgeUiModule::requestRedraw()
{
    UIFrameEvent event;
    event.action = UIFrameEvent::Action::REDRAW_ONLY;
    notifyObservers(&event);
}

size_t BridgeUiModule::collectLines(UiLine *lines, size_t max)
{
    if (bridgeThread == nullptr)
        return 0;

    if (nav_.page == UiPage::Settings)
        return buildSettingsLines(draft_, nav_.cursor, nav_.editing, lines, max);

    UiStatus status;
    status.running = true;
    status.mode = bridgeThread->mode();
    status.packetsHeard = bridgeThread->radio().packetsReceived();
    status.packetsSent = bridgeThread->radio().packetsSent();
    status.mirrored = mirrorModule != nullptr ? mirrorModule->mirror().mirroredCount() : 0;
    status.injected = mirrorModule != nullptr ? mirrorModule->injectedCount() : 0;
    status.meshcoreDutyCycle = bridgeThread->stats().meshcoreDutyCycle();
    status.freeHeapBytes = ESP.getFreeHeap();
    const LoraProfile host = bridgeThread->radio().currentMeshtasticProfile();
    status.blocker = alignmentBlocker(host, bridgeThread->settings().meshcore);
    status.hostToleratesScanning = hostToleratesScanning(host, bridgeThread->settings().meshcore, bridgeThread->mode());

    MeshcoreStack *stack = bridgeThread->meshcore().stack();
    if (stack != nullptr) {
        status.lastText = stack->lastText();
        status.adverts = stack->advertsSent();
    }

    return buildStatusLines(bridgeThread->settings(), status, lines, max);
}

void BridgeUiModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (display == nullptr || state == nullptr)
        return;

    lastDrawMs_ = millis();

    UiLine lines[kUiMaxLines];
    const size_t count = collectLines(lines, kUiMaxLines);

    graphics::drawCommonHeader(display, x, y, count > 0 ? lines[0].text : "MeshCore");

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    if (count == 0) {
        display->drawString(x, graphics::getTextPositions(display)[1], "not started");
        return;
    }

    const int *rows = graphics::getTextPositions(display);

    const int available = display->getHeight() - rows[1];
    size_t fit = available > 0 ? static_cast<size_t>(available) / static_cast<size_t>(FONT_HEIGHT_SMALL) : 0;
    if (fit < 1)
        fit = 1;
    const size_t bodyRows = fit < kBodyRows ? fit : kBodyRows;

    const size_t first = nav_.page == UiPage::Settings ? visibleWindowStart(nav_.cursor, bodyRows) : 1;

    UiRowPlan plan[kBodyRows];
    const size_t planned = planRows(lines, count, first, bodyRows, plan, kBodyRows);

    for (size_t i = 0; i < planned; i++) {
        if (plan[i].inverted) {
            display->fillRect(0, rows[plan[i].row] + 2, display->getWidth(), FONT_HEIGHT_SMALL - 5);
            display->setColor(BLACK);
        }

        display->drawString(x + 2, rows[plan[i].row], lines[plan[i].lineIndex].text);
        display->setColor(WHITE);
    }
}

void BridgeUiModule::commit()
{
    if (!validateSettings(draft_)) {
        LOG_WARN("MeshCompromise draft settings invalid, reverting");
        if (bridgeThread != nullptr)
            draft_ = bridgeThread->settings();
        return;
    }

    if (bridgeThread != nullptr) {
        LOG_INFO("MeshCompromise applying edited settings");
        bridgeThread->applySettings(draft_);
        draft_ = bridgeThread->settings();
    }
}

int BridgeUiModule::handleInput(const InputEvent *event)
{
    if (event == nullptr || bridgeThread == nullptr)
        return 0;

    const uint32_t now = millis();
    if (lastDrawMs_ == 0 || now - lastDrawMs_ > kUiFocusGraceMs) {
        if (nav_.editing || nav_.page != UiPage::Status) {
            LOG_DEBUG("MeshCompromise UI lost focus, discarding unsaved edits");
            nav_ = UiNavState();
            draft_ = bridgeThread->settings();
        }
        return 0;
    }

    const UiNavResult result = navigate(nav_, uiKeyFor(event), now);
    if (!result.handled)
        return 0;

    if (result.reloadDraft)
        draft_ = bridgeThread->settings();

    if (result.adjust != 0)
        adjustSetting(draft_, static_cast<SettingField>(nav_.cursor), result.adjust);

    if (result.commit)
        commit();

    LOG_DEBUG("MeshCompromise UI page=%d cursor=%u editing=%d adjust=%d", static_cast<int>(nav_.page),
              static_cast<unsigned>(nav_.cursor), nav_.editing ? 1 : 0, static_cast<int>(result.adjust));

    if (result.redraw)
        requestRedraw();

    return 1;
}

void setupBridgeUi()
{
    if (bridgeUiModule == nullptr)
        bridgeUiModule = new BridgeUiModule();
}

} // namespace meshcompromise

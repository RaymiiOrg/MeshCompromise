#pragma once

#include "Observer.h"
#include "input/InputBroker.h"
#include "mesh/MeshModule.h"
#include "meshcompromise/settings.h"
#include "meshcompromise/ui_nav.h"
#include "meshcompromise/ui_text.h"

namespace meshcompromise
{

class BridgeUiModule : public MeshModule, public Observable<const UIFrameEvent *>
{
  public:
    BridgeUiModule();

    bool wantUIFrame() override;
    Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;

    bool interceptingKeyboardInput() override { return nav_.page == UiPage::Settings; }

    int handleInput(const InputEvent *event);

  protected:
    bool wantPacket(const meshtastic_MeshPacket *) override { return false; }

  private:
    void commit();
    void requestRedraw();
    size_t collectLines(UiLine *lines, size_t max);

    CallbackObserver<BridgeUiModule, const InputEvent *> inputObserver =
        CallbackObserver<BridgeUiModule, const InputEvent *>(this, &BridgeUiModule::handleInput);

    UiNavState nav_;
    uint32_t lastDrawMs_ = 0;
    BridgeSettings draft_;
};

extern BridgeUiModule *bridgeUiModule;

void setupBridgeUi();

} // namespace meshcompromise

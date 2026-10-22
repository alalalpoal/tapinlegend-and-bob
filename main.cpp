#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

// Gamemodes where a click only does something when the player is on the
// ground or touching an orb. In ship/ufo/wave/swing a click always works,
// so there is nothing to buffer.
static bool needsGroundOrOrb(PlayerObject* p) {
    return !(p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing);
}

// True if a click on this exact physics step would do something.
static bool canAct(PlayerObject* p) {
    return p->m_isOnGround || p->m_touchedRing != nullptr;
}

class $modify(BufferGameLayer, GJBaseGameLayer) {
    struct Pending {
        bool active = false;
        bool released = false; // player let go while we were still holding the press back
        int left = 0;
    };

    struct Fields {
        Pending pending[2][4];          // [player1 ? 0 : 1][button]
        bool releaseQueued[2][4] = {};  // release to send on the next step
        bool passthrough = false;       // true while we replay inputs ourselves
    };

    // Called for every press/release (keyboard, touch, controller).
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& f = *m_fields;

        auto* p = isPlayer1 ? m_player1 : m_player2;
        if (f.passthrough || !p || button < 0 || button > 3 ||
            !Mod::get()->getSettingValue<bool>("enabled")) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        int idx = isPlayer1 ? 0 : 1;
        auto& pend = f.pending[idx][button];

        if (down) {
            if (needsGroundOrOrb(p) && !canAct(p)) {
                // Too early: hold the press back and retry it every step.
                pend.active = true;
                pend.released = false;
                pend.left = static_cast<int>(Mod::get()->getSettingValue<int64_t>("buffer-frames"));
                return;
            }
            pend = Pending{};
        } else if (pend.active) {
            // Released before the buffered press fired: remember it, send later.
            pend.released = true;
            return;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Runs once per physics step.
    void processCommands(float dt) {
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            this->tickBuffers();
        }
        GJBaseGameLayer::processCommands(dt);
    }

    void tickBuffers() {
        auto& f = *m_fields;
        f.passthrough = true;

        for (int idx = 0; idx < 2; idx++) {
            auto* p = idx == 0 ? m_player1 : m_player2;
            for (int b = 0; b < 4; b++) {
                bool isP1 = idx == 0;
                auto& pend = f.pending[idx][b];

                // Send a delayed release from the previous step.
                if (f.releaseQueued[idx][b]) {
                    f.releaseQueued[idx][b] = false;
                    GJBaseGameLayer::handleButton(false, b, isP1);
                }

                if (!pend.active) continue;

                if (!p || p->m_isDead) {
                    pend = Pending{};
                    continue;
                }

                bool fire = !needsGroundOrOrb(p) || canAct(p);

                if (!fire && --pend.left <= 0) {
                    if (pend.released) {
                        // Window ran out and the tap was already over: it was a miss.
                        pend = Pending{};
                        continue;
                    }
                    // Still holding: send it late so hold behaviour matches vanilla.
                    fire = true;
                }

                if (fire) {
                    GJBaseGameLayer::handleButton(true, b, isP1);
                    if (pend.released) f.releaseQueued[idx][b] = true;
                    pend = Pending{};
                }
            }
        }

        f.passthrough = false;
    }
};

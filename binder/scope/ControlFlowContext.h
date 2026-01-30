//
// Control flow context for tracking loop and switch depth
//

#ifndef DJINN_CONTROL_FLOW_CONTEXT_H
#define DJINN_CONTROL_FLOW_CONTEXT_H

namespace djinn::binder {
    class ControlFlowContext {
    public:
        void enterLoop() { _loopDepth++; }
        void exitLoop() { _loopDepth--; }
        void enterSwitch() { _switchDepth++; }
        void exitSwitch() { _switchDepth--; }

        [[nodiscard]] bool canBreak() const { return _loopDepth > 0 || _switchDepth > 0; }
        [[nodiscard]] bool canContinue() const { return _loopDepth > 0; }
        [[nodiscard]] bool inLoop() const { return _loopDepth > 0; }
        [[nodiscard]] bool inSwitch() const { return _switchDepth > 0; }

        // RAII helper for loop scope
        class LoopGuard {
        public:
            explicit LoopGuard(ControlFlowContext &ctx) : _ctx(ctx) { _ctx.enterLoop(); }
            ~LoopGuard() { _ctx.exitLoop(); }

            LoopGuard(const LoopGuard &) = delete;

            LoopGuard &operator=(const LoopGuard &) = delete;

        private:
            ControlFlowContext &_ctx;
        };

        // RAII helper for switch scope
        class SwitchGuard {
        public:
            explicit SwitchGuard(ControlFlowContext &ctx) : _ctx(ctx) { _ctx.enterSwitch(); }
            ~SwitchGuard() { _ctx.exitSwitch(); }

            SwitchGuard(const SwitchGuard &) = delete;

            SwitchGuard &operator=(const SwitchGuard &) = delete;

        private:
            ControlFlowContext &_ctx;
        };

    private:
        int _loopDepth = 0;
        int _switchDepth = 0;
    };
} // namespace djinn::binder

#endif // DJINN_CONTROL_FLOW_CONTEXT_H
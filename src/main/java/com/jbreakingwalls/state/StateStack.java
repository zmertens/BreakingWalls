package com.jbreakingwalls.state;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.function.Supplier;

/**
 * Stack-based game-state manager — mirrors StateStack from the C++ source.
 *
 * States are lazily constructed via registered factory lambdas.
 * Transitions (push/pop/replace) are deferred to the end of the current
 * update tick so that in-progress iteration is never invalidated.
 */
public final class StateStack {

    private enum PendingAction { PUSH, POP, REPLACE, CLEAR }

    // record provides value semantics without boilerplate
    private record Pending(PendingAction action, State.ID id) {}

    // ArrayDeque: in Java 21 also implements SequencedCollection — O(1) push/pop/peek
    private final Deque<State>                   stack     = new ArrayDeque<>();
    // EnumMap: array-backed by ordinal — O(1) get/put with near-zero GC pressure
    private final Map<State.ID, Supplier<State>> factories = new EnumMap<>(State.ID.class);
    // Second ArrayDeque for deferred changes — avoids ConcurrentModificationException
    private final Deque<Pending>                 pending   = new ArrayDeque<>();

    private final Context ctx;

    public StateStack(Context ctx) {
        this.ctx = ctx;
    }

    // ── Registration ──────────────────────────────────────────────────────────

    public void registerState(State.ID id, Supplier<State> factory) {
        factories.put(id, factory);
    }

    // ── Deferred transition API ───────────────────────────────────────────────

    public void pushState(State.ID id)    { pending.add(new Pending(PendingAction.PUSH,    id)); }
    public void popState()                { pending.add(new Pending(PendingAction.POP,     null)); }
    public void replaceState(State.ID id) { pending.add(new Pending(PendingAction.REPLACE, id)); }
    public void clearAll()                { pending.add(new Pending(PendingAction.CLEAR,   null)); }

    // ── Per-frame interface ───────────────────────────────────────────────────

    public void update(float dt) {
        for (State s : stack) {
            if (!s.update(dt)) break;
        }
        applyPending();
    }

    public void draw() {
        // List.copyOf gives an immutable snapshot — safe if a state pushes during draw
        // .reversed() is a Java 21 SequencedCollection view — O(0), no second copy
        for (State s : List.copyOf(stack).reversed()) {
            s.draw();
        }
    }

    public void handleKey(int key, int action) {
        for (State s : stack) {
            if (!s.handleKey(key, action)) break;
        }
    }

    public boolean isEmpty() { return stack.isEmpty(); }
    public int     size()    { return stack.size(); }

    // ── Private helpers ───────────────────────────────────────────────────────

    private void applyPending() {
        while (!pending.isEmpty()) {
            Pending p = pending.poll();
            // Java 21 switch expression with arrow cases — exhaustive over sealed enum,
            // compiled to tableswitch bytecode: O(1) dispatch with no fall-through bugs
            switch (p.action()) {
                case PUSH -> {
                    State s = createState(p.id());
                    s.onEnter();
                    stack.push(s); // O(1) on ArrayDeque
                }
                case POP -> {
                    if (!stack.isEmpty()) {
                        stack.peek().onExit();
                        stack.pop();
                    }
                }
                case REPLACE -> {
                    if (!stack.isEmpty()) {
                        stack.peek().onExit();
                        stack.pop();
                    }
                    State s = createState(p.id());
                    s.onEnter();
                    stack.push(s);
                }
                case CLEAR -> {
                    // forEach with method reference — JIT may devirtualise the call
                    stack.forEach(State::onExit);
                    stack.clear();
                }
            }
        }
    }

    private State createState(State.ID id) {
        Supplier<State> factory = factories.get(id);
        if (factory == null) {
            throw new IllegalStateException("No factory registered for state: " + id);
        }
        return factory.get();
    }
}

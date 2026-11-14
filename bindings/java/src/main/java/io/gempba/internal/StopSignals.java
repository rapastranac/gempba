package io.gempba.internal;

import java.util.HashMap;
import java.util.Map;

import sun.misc.Signal;
import sun.misc.SignalHandler;

/**
 * Lets {@code SIGINT} and {@code SIGTERM} stop a run gracefully inside a JVM.
 *
 * <p>The JVM handles both signals itself, to run its shutdown hooks, so the native library leaves them alone (#443).
 * These handlers request the stop instead of starting a shutdown. A second {@code SIGINT} goes to the JVM's own handler,
 * which shuts down as usual; {@code SIGTERM} stays taken, as in C++.
 */
public final class StopSignals {

    private static final Object LOCK = new Object();
    private static final Map<String, SignalHandler> PREVIOUS = new HashMap<>();
    private static boolean enabled = true;
    private static boolean wanted = false;

    private StopSignals() {
    }

    /**
     * Takes both signals, unless {@link #setEnabled} turned that off.
     */
    public static void take() {
        synchronized (LOCK) {
            wanted = true;
            if (enabled) {
                takeAll();
            }
        }
    }

    /**
     * Gives both signals back to the handlers they had.
     */
    public static void release() {
        synchronized (LOCK) {
            wanted = false;
            giveBackAll();
        }
    }

    /**
     * Turns taking the signals on or off; turning it off gives them back at once.
     *
     * @param on whether the signals stop the run gracefully
     */
    public static void setEnabled(boolean on) {
        synchronized (LOCK) {
            enabled = on;
            if (!on) {
                giveBackAll();
            } else if (wanted) {
                takeAll();
            }
        }
    }

    /**
     * @param name the signal's name without {@code SIG}, such as {@code "TERM"}
     * @return whether the signal is taken
     */
    public static boolean isTaken(String name) {
        synchronized (LOCK) {
            return PREVIOUS.containsKey(name);
        }
    }

    private static void takeAll() {
        take("INT", true);
        take("TERM", false);
    }

    private static void giveBackAll() {
        for (String name : PREVIOUS.keySet().toArray(new String[0])) {
            giveBack(name);
        }
    }

    private static void take(String name, boolean once) {
        if (PREVIOUS.containsKey(name)) {
            return;
        }
        SignalHandler ours = signal -> {
            GemPBANative.Stop.requestStopOnSignal(signal.getNumber());
            if (once) {
                synchronized (LOCK) {
                    giveBack(name);
                }
            }
        };
        try {
            PREVIOUS.put(name, Signal.handle(new Signal(name), ours));
        } catch (IllegalArgumentException ignored) {
        }
    }

    private static void giveBack(String name) {
        SignalHandler previous = PREVIOUS.remove(name);
        if (previous != null) {
            Signal.handle(new Signal(name), previous);
        }
    }
}

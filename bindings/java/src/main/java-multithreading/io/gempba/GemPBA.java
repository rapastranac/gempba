package io.gempba;

import io.gempba.core.LoadBalancer;
import io.gempba.core.Node;
import io.gempba.core.NodeManager;
import io.gempba.task.ClosureTask;
import io.gempba.task.Deserializer;
import io.gempba.task.NodeTask;
import io.gempba.task.ResultClosureTask;
import io.gempba.task.Serializer;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;

import java.util.function.BooleanSupplier;

/**
 * Main entry point for the gempba Java binding (multithreading variant).
 *
 * <p>This class mirrors the C++ {@code gempba::} namespace plus the
 * {@code gempba::multithreading::} sub-namespace.  In a multithreading-only
 * build the C++ side makes {@code multithreading} the inline namespace, so
 * consumers write {@code gempba::create_load_balancer(...)} without typing
 * {@code multithreading::} — this Java class provides the same flat surface.
 *
 * <p>The multiprocessing variant of this JAR ships a different {@code GemPBA}
 * class with a different factory surface (see {@code Scheduler},
 * {@link #createLoadBalancer(BalancingPolicy)} vs. its MP overload, etc.) and
 * mutually-exclusive imports, so a project depending on
 * {@code gempba:<version>:multithreading} will not see any MP type on
 * its classpath.
 *
 * <h2>Typical usage</h2>
 * <pre>{@code
 * LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
 * NodeManager  nm = GemPBA.createNodeManager(lb);
 *
 * nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
 * nm.setThreadPoolSize(4);
 * nm.setScore(Score.make(0));
 *
 * Node seed = GemPBA.createSeedNode(
 *     lb, MySearch::explore, 5,
 *     v -> ByteBuffer.allocate(4).putInt(v).array(),
 *     b -> ByteBuffer.wrap(b).getInt());
 *
 * nm.tryLocalSubmit(seed);
 * nm.waitForCompletion();
 * System.exit(GemPBA.shutdown());
 * }</pre>
 */
public final class GemPBA {

    private GemPBA() {
    }

    // ─── Mode-agnostic entry points (mirror gempba:: free functions) ─────────

    /**
     * Returns the singleton {@link NodeManager} previously created via
     * {@link #createNodeManager}.
     *
     * <p>Equivalent to {@code gempba::get_node_manager()} in C++.
     */
    public static NodeManager getNodeManager() {
        return new NodeManager(GemPBANative.getNodeManagerHandle());
    }

    /**
     * Returns the singleton {@link LoadBalancer} previously created via
     * {@link #createLoadBalancer}.
     *
     * <p>Equivalent to {@code gempba::get_load_balancer()} in C++.
     */
    public static LoadBalancer getLoadBalancer() {
        return new LoadBalancer(GemPBANative.getLoadBalancerHandle());
    }

    /**
     * Shuts down gempba and releases all native resources.
     *
     * <p>Equivalent to {@code gempba::shutdown()} in C++.
     *
     * @return exit code (pass to {@link System#exit})
     */
    public static int shutdown() {
        return GemPBANative.shutdown();
    }

    /**
     * Creates a placeholder node with no runnable.
     *
     * <p>Equivalent to {@code gempba::create_dummy_node(lb)} in C++.  Use as
     * the initial parent when the top-level search call has no real parent
     * node (e.g. the outermost entry into a closure-based tree traversal).
     *
     * @param lb the load balancer this node belongs to
     * @return a dummy node suitable for use as a parent in
     * {@link #createExplicitNode(LoadBalancer, Node, ClosureTask)}
     */
    public static Node createDummyNode(LoadBalancer lb) {
        Node node = new Node(lb.handle, (tid, args, h) -> null);
        node.state.handle = GemPBANative.createDummyNode(lb.handle);
        return node;
    }

    /**
     * Creates the root node of the search tree.
     *
     * <p>Equivalent to {@code gempba::create_seed_node<Ret>(lb, fn, args)} in C++.
     * All type knowledge ({@code task}, {@code serializer}, {@code deserializer})
     * is captured in the node's internal closure; the returned {@link Node} is
     * a plain, non-generic handle.
     *
     * @param lb           the load balancer that will own this subtree
     * @param task         the branching function executed at each node
     * @param initialArgs  starting arguments for the root invocation
     * @param serializer   converts {@code Args} → {@code byte[]} at the JNI boundary
     * @param deserializer converts {@code byte[]} → {@code Args} from the JNI boundary
     * @param <Args>       bundled argument type
     * @param <R>          result type
     * @return the seed node, ready to pass to {@link NodeManager#tryLocalSubmit}
     */
    public static <Args, R> Node createSeedNode(
            LoadBalancer lb,
            NodeTask<Args, R> task,
            Args initialArgs,
            Serializer<Args> serializer,
            Deserializer<Args> deserializer) {

        Node.Executor exec = (tid, argBytes, selfHandle) -> {
            Args args = deserializer.deserialize(argBytes);
            Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
            task.execute(tid, args, ctx);
            return null;
        };

        Node node = new Node(lb.handle, exec);
        node.state.handle = GemPBANative.createSeedNode(lb.handle, node, serializer.serialize(initialArgs));
        return node;
    }

    // ─── Telemetry (process-wide flag) ───────────────────────────────────────

    /**
     * Re-enables the telemetry hub install path.  Subsequent
     * {@link #createNodeManager} calls (and any internal hub re-install) will
     * bring up the telemetry sidecar.  No-op when telemetry is already enabled.
     *
     * <p>Equivalent to {@code gempba::telemetry::enable()} in C++.
     */
    public static void enableTelemetry() {
        GemPBANative.Telemetry.enable();
    }

    /**
     * Disables the telemetry hub install path until {@link #enableTelemetry}
     * is called.  Subsequent factory calls will skip the hub install.
     *
     * <p>Equivalent to {@code gempba::telemetry::disable()} in C++.
     */
    public static void disableTelemetry() {
        GemPBANative.Telemetry.disable();
    }

    /**
     * Reports this process's telemetry-enabled flag.
     *
     * <p>Equivalent to {@code gempba::telemetry::is_enabled()} in C++.
     *
     * @return {@code true} while installs are allowed (default), {@code false}
     * after {@link #disableTelemetry} and before the matching {@link #enableTelemetry}
     */
    public static boolean isTelemetryEnabled() {
        return GemPBANative.Telemetry.isEnabled();
    }

    /**
     * Sets the loopback TCP port the telemetry hub binds (default 9000).  Must
     * be called before the first {@link #createNodeManager} installs a hub; a
     * later call does not move an already-bound hub.
     *
     * <p>Equivalent to {@code gempba::telemetry::configure_port(port)} in C++.
     *
     * @param port TCP port, 0–65535
     * @throws IllegalArgumentException if {@code port} is outside 0–65535
     */
    public static void configureTelemetryPort(int port) {
        if (port < 0 || port > 0xFFFF) {
            throw new IllegalArgumentException("telemetry port must be in 0..65535, got " + port);
        }
        GemPBANative.Telemetry.configurePort(port);
    }

    // ─── Multithreading factories (mirror gempba::multithreading::) ─────────

    /**
     * Creates a multithreaded load balancer with the given strategy.
     *
     * <p>Equivalent to {@code gempba::multithreading::create_load_balancer(policy)}
     * in C++.
     *
     * @param policy load-balancing strategy
     * @return a new {@link LoadBalancer}
     */
    public static LoadBalancer createLoadBalancer(BalancingPolicy policy) {
        long handle = GemPBANative.MT.createLoadBalancer(policy.ordinal());
        return new LoadBalancer(handle);
    }

    /**
     * Creates a multithreaded node manager backed by the given load balancer.
     *
     * <p>Equivalent to {@code gempba::multithreading::create_node_manager(lb)}
     * in C++.
     *
     * @param lb the load balancer to associate with this node manager
     * @return a new {@link NodeManager}
     */
    public static NodeManager createNodeManager(LoadBalancer lb) {
        long handle = GemPBANative.MT.createNodeManager(lb.handle);
        return new NodeManager(handle);
    }

    /**
     * Creates a closure-mode explicit child node.
     *
     * <p>Equivalent to {@code gempba::multithreading::create_explicit_node(lb, parent, fn, args)}
     * in C++.  The arguments are captured inside the Java lambda rather than
     * serialised to bytes — the preferred form for single-machine
     * (multithreading) use.
     *
     * @param lb     the load balancer that will schedule this node
     * @param parent the parent node; may be a dummy node at the root level
     * @param task   closure that performs the work when the framework executes this node
     * @return the new child node, ready to pass to
     * {@link NodeManager#tryLocalSubmit} or {@link NodeManager#forward}
     */
    public static Node createExplicitNode(LoadBalancer lb, Node parent, ClosureTask task) {
        Node child = new Node(lb.handle, (tid, argBytes, selfHandle) -> {
            Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
            task.run(tid, ctx);
            return null;
        });
        child.state.handle = GemPBANative.MT.createExplicitNode(lb.handle, parent.state.handle, child, null);
        return child;
    }

    /**
     * Creates a serialisation-mode explicit child node.
     *
     * <p>Equivalent to {@code gempba::multithreading::create_explicit_node(lb, parent, fn, args)}
     * in C++.  Arguments are serialised to bytes, enabling the same task function
     * to be reused across multiple node levels via method references.
     *
     * @param lb           the load balancer that will schedule this node
     * @param parent       the parent node
     * @param task         the branching function for this node
     * @param args         arguments for the child node
     * @param serializer   converts {@code Args} → {@code byte[]}
     * @param deserializer converts {@code byte[]} → {@code Args}
     * @param <Args>       argument type
     * @param <R>          result type
     * @return the new child node
     */
    public static <Args, R> Node createExplicitNode(
            LoadBalancer lb, Node parent, NodeTask<Args, R> task, Args args,
            Serializer<Args> serializer, Deserializer<Args> deserializer) {
        Node child = new Node(lb.handle, (tid, argBytes, selfHandle) -> {
            Args a = deserializer.deserialize(argBytes);
            Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
            task.execute(tid, a, ctx);
            return null;
        });
        child.state.handle = GemPBANative.MT.createExplicitNode(
                lb.handle, parent.state.handle, child, serializer.serialize(args));
        return child;
    }

    /**
     * Creates a closure-mode lazy child node.
     *
     * <p>Equivalent to {@code gempba::multithreading::create_lazy_node(lb, parent, fn, init)}
     * in C++.  The framework evaluates {@code gate} just before executing the node:
     * if it returns {@code false} the node is pruned; if {@code true} the task runs.
     *
     * @param lb     the load balancer that will schedule this node
     * @param parent the parent node
     * @param gate   evaluated at execution time; {@code false} prunes the node
     * @param task   closure executed when the gate passes
     * @return the new lazy child node
     */
    public static Node createLazyNode(
            LoadBalancer lb, Node parent,
            BooleanSupplier gate, ClosureTask task) {
        Node child = new Node(lb.handle,
                (tid, argBytes, selfHandle) -> {
                    Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
                    task.run(tid, ctx);
                    return null;
                },
                () -> gate.getAsBoolean() ? Node.EMPTY_BYTES : null);
        child.state.handle = GemPBANative.MT.createLazyNode(lb.handle, parent.state.handle, child, null);
        return child;
    }

    // ─── Non-void overloads (mirror gempba::create_*<R>) ────────────────────

    /**
     * Creates a closure-mode explicit child node whose task body returns a
     * typed value retrievable later via {@link Node#getResult()}.
     *
     * <p>Equivalent to {@code gempba::create_explicit_node<R>(lb, parent, fn, ...)}
     * in C++ — the C++ layer's std::any-based result pipeline is bridged here
     * by the supplied {@link Serializer}.  Pair with the matching
     * {@link Deserializer} on the consumer side.
     *
     * @param lb               the load balancer that will schedule this node
     * @param parent           the parent node
     * @param task             closure that performs the work and returns a typed result
     * @param resultSerializer encodes the task's return value to {@code byte[]}
     * @param <R>              return type of the task body
     * @return the new child node; call {@link Node#getResult()} to retrieve the
     * serialised result bytes (blocks until the runnable finishes)
     */
    public static <R> Node createExplicitNode(LoadBalancer lb, Node parent,
                                              ResultClosureTask<R> task,
                                              Serializer<R> resultSerializer) {
        Node child = new Node(lb.handle, (tid, argBytes, selfHandle) -> {
            Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
            R result = task.run(tid, ctx);
            return result == null ? null : resultSerializer.serialize(result);
        });
        child.state.handle = GemPBANative.MT.createExplicitNode(lb.handle, parent.state.handle, child, null);
        return child;
    }

    /**
     * Creates a serialisation-mode explicit child node whose task body returns
     * a typed value retrievable later via {@link Node#getResult()}.
     *
     * <p>Equivalent to {@code gempba::create_explicit_node<R>(lb, parent, fn, args, ...)}
     * in C++.
     *
     * @param lb               the load balancer that will schedule this node
     * @param parent           the parent node
     * @param task             the branching function for this node
     * @param args             arguments for the child node
     * @param argsSerializer   encodes {@code Args} → {@code byte[]}
     * @param argsDeserializer decodes {@code byte[]} → {@code Args}
     * @param resultSerializer encodes the task's return value to {@code byte[]}
     * @param <Args>           argument type
     * @param <R>              return type of the task body
     * @return the new child node; call {@link Node#getResult()} to retrieve the
     * serialised result bytes (blocks until the runnable finishes)
     */
    public static <Args, R> Node createExplicitNode(
            LoadBalancer lb, Node parent, NodeTask<Args, R> task, Args args,
            Serializer<Args> argsSerializer, Deserializer<Args> argsDeserializer,
            Serializer<R> resultSerializer) {
        Node child = new Node(lb.handle, (tid, argBytes, selfHandle) -> {
            Args a = argsDeserializer.deserialize(argBytes);
            Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
            R result = task.execute(tid, a, ctx);
            return result == null ? null : resultSerializer.serialize(result);
        });
        child.state.handle = GemPBANative.MT.createExplicitNode(
                lb.handle, parent.state.handle, child, argsSerializer.serialize(args));
        return child;
    }

    /**
     * Creates a closure-mode lazy child node whose task body returns a typed
     * value retrievable later via {@link Node#getResult()}.
     *
     * <p>Equivalent to {@code gempba::create_lazy_node<R>(lb, parent, fn, init)}
     * in C++.  The framework evaluates {@code gate} just before executing the
     * node: {@code false} prunes (and {@link Node#getResult()} returns
     * {@code null}); {@code true} runs the task and exposes its serialised
     * return value.
     *
     * @param lb               the load balancer that will schedule this node
     * @param parent           the parent node
     * @param gate             evaluated at execution time; {@code false} prunes the node
     * @param task             closure executed when the gate passes
     * @param resultSerializer encodes the task's return value to {@code byte[]}
     * @param <R>              return type of the task body
     * @return the new lazy child node
     */
    public static <R> Node createLazyNode(
            LoadBalancer lb, Node parent,
            BooleanSupplier gate, ResultClosureTask<R> task,
            Serializer<R> resultSerializer) {
        Node child = new Node(lb.handle,
                (tid, argBytes, selfHandle) -> {
                    Node ctx = (selfHandle == 0L) ? null : Node.ofHandle(lb.handle, selfHandle);
                    R result = task.run(tid, ctx);
                    return result == null ? null : resultSerializer.serialize(result);
                },
                () -> gate.getAsBoolean() ? Node.EMPTY_BYTES : null);
        child.state.handle = GemPBANative.MT.createLazyNode(lb.handle, parent.state.handle, child, null);
        return child;
    }
}

package io.gempba;

import io.gempba.core.LoadBalancer;
import io.gempba.core.Node;
import io.gempba.core.NodeManager;
import io.gempba.scheduler.Scheduler;
import io.gempba.scheduler.SchedulerTopology;
import io.gempba.scheduler.SerialRunnable;
import io.gempba.scheduler.SerialTask;
import io.gempba.task.Deserializer;
import io.gempba.task.NodeTask;
import io.gempba.task.Serializer;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;

/**
 * Main entry point for the gempba Java binding (multiprocessing variant).
 *
 * <p>This class mirrors the C++ {@code gempba::} namespace plus the
 * {@code gempba::multiprocessing::} sub-namespace.  In a multiprocessing
 * build the C++ side makes {@code multiprocessing} the inline namespace, so
 * consumers write {@code gempba::create_load_balancer(policy, worker)} without
 * typing {@code multiprocessing::} — this Java class provides the same flat
 * surface.
 *
 * <p>The multithreading variant of this JAR ships a different {@code GemPBA}
 * class with a different factory surface and mutually-exclusive imports
 * (no {@link Scheduler} / {@link SerialRunnable}), so a project depending on
 * {@code gempba:<version>:multithreading} will not see any MP type on
 * its classpath.
 *
 * <h2>Typical usage</h2>
 * <pre>{@code
 * Scheduler sched = GemPBA.createScheduler(SchedulerTopology.SEMI_CENTRALIZED, 3.0);
 * sched.setGoal(Goal.MAXIMISE, ScoreType.I32);
 *
 * if (sched.rankMe() == 0) {
 *     sched.center().run(initialTaskBytes, RUNNABLE_ID);
 *     byte[] result = sched.center().getResult();
 * } else {
 *     LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL, sched.worker());
 *     NodeManager  nm = GemPBA.createNodeManager(lb, sched.worker());
 *     nm.setThreadPoolSize(4);
 *     nm.setScore(Score.make(Integer.MIN_VALUE));
 *
 *     Map<Integer, SerialRunnable> runnables = Map.of(
 *         RUNNABLE_ID, GemPBA.runnableReturnNone(RUNNABLE_ID, (tid, bytes) -> explore(tid, bytes)));
 *     sched.worker().run(nm, runnables);
 * }
 * GemPBA.shutdown();
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
     * <p>Equivalent to {@code gempba::create_dummy_node(lb)} in C++.
     *
     * @param lb the load balancer this node belongs to
     * @return a dummy node suitable for use as a parent in
     * {@link #createExplicitNode(LoadBalancer, Node, NodeTask, Object, Serializer, Deserializer)}
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
     * {@link #createNodeManager} / {@link #createScheduler} calls (and any
     * internal hub re-install) will bring up the telemetry sidecar.
     *
     * <p>Equivalent to {@code gempba::telemetry::enable()} in C++.
     *
     * <p><strong>MP symmetry:</strong> the C++ docs require this be called on
     * every rank or none — an asymmetric call will deadlock the next install's
     * collective IPC handshake.
     */
    public static void enableTelemetry() {
        GemPBANative.Telemetry.enable();
    }

    /**
     * Disables the telemetry hub install path until {@link #enableTelemetry}
     * is called.  Subsequent factory calls will skip the hub install.
     *
     * <p>Equivalent to {@code gempba::telemetry::disable()} in C++.
     *
     * <p><strong>MP symmetry:</strong> the C++ docs require this be called on
     * every rank or none — an asymmetric call will deadlock the next install's
     * collective IPC handshake.
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

    // ─── Multiprocessing factories (mirror gempba::multiprocessing::) ───────

    /**
     * Creates a distributed scheduler with the given topology and per-process
     * timeout in seconds.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::create_scheduler(topology, timeout)}
     * in C++.  Must be called on every process in the communicator.
     *
     * @param topology distributed coordination pattern
     * @param timeout  per-process search timeout in seconds
     * @return a new {@link Scheduler}
     */
    public static Scheduler createScheduler(SchedulerTopology topology, double timeout) {
        long handle = GemPBANative.Scheduler.create(topology.ordinal(), timeout);
        return new Scheduler(handle);
    }

    /**
     * Creates an MP load balancer, wiring in the scheduler worker view so
     * that {@link NodeManager#tryRemoteSubmit} can dispatch tasks to other
     * MPI ranks.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::create_load_balancer(policy, worker)}
     * in C++.  Pass {@code null} for the center rank (rank 0).
     *
     * @param policy load-balancing strategy
     * @param worker scheduler worker view, or {@code null} for rank 0
     * @return a new {@link LoadBalancer}
     */
    public static LoadBalancer createLoadBalancer(BalancingPolicy policy, Scheduler.Worker worker) {
        long workerHandle = (worker != null) ? worker.handle : 0L;
        long handle = GemPBANative.MP.createLoadBalancer(policy.ordinal(), workerHandle);
        return new LoadBalancer(handle);
    }

    /**
     * Creates an MP node manager backed by {@code lb} and linked to the
     * scheduler worker view.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::create_node_manager(lb, worker)}
     * in C++.  Pass {@code null} for the center rank (rank 0).
     *
     * @param lb     the load balancer backing this node manager
     * @param worker scheduler worker view, or {@code null} for rank 0
     * @return a new {@link NodeManager}
     */
    public static NodeManager createNodeManager(LoadBalancer lb, Scheduler.Worker worker) {
        long workerHandle = (worker != null) ? worker.handle : 0L;
        long handle = GemPBANative.MP.createNodeManager(lb.handle, workerHandle);
        return new NodeManager(handle);
    }

    /**
     * Creates an MP explicit child node with serialised arguments.
     *
     * <p>The serialised bytes are stored inside the node so that
     * {@link NodeManager#tryRemoteSubmit} can pack them for dispatch to
     * another MPI process.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::create_explicit_node(lb, parent, fn, args, ser, deser)}
     * in C++.
     *
     * @param lb           the load balancer that will schedule this node
     * @param parent       the parent node
     * @param task         the branching function for this node
     * @param args         arguments for the child node
     * @param serializer   converts {@code Args} → {@code byte[]}
     * @param deserializer converts {@code byte[]} → {@code Args}
     * @param <Args>       argument type
     * @param <R>          result type
     * @return the new child node, ready for {@link NodeManager#tryRemoteSubmit}
     * or {@link NodeManager#forward}
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
        child.state.handle = GemPBANative.MP.createExplicitNode(lb.handle, parent.state.handle, child, serializer.serialize(args));
        return child;
    }

    /**
     * Creates an MP explicit child node whose task body returns a typed value
     * retrievable later via {@link Node#getResult()}.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::create_explicit_node<R>(lb, parent, fn, args, ser, deser)}
     * in C++ — the C++ layer's std::any-based result pipeline is bridged here
     * by the supplied result {@link Serializer}.  Pair with the matching
     * {@link Deserializer} on the consumer side.
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
        child.state.handle = GemPBANative.MP.createExplicitNode(
                lb.handle, parent.state.handle, child, argsSerializer.serialize(args));
        return child;
    }

    // ─── Serial runnable factories (mirror gempba::multiprocessing::runnables::) ─

    /**
     * Creates a void serial runnable — the result bytes are discarded.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::runnables::return_none::create(id, fn, deser)}
     * in C++.  Use when the worker does not need to send a result back to
     * the center.
     *
     * @param id   integer ID used to look up this runnable in the worker's map
     * @param task body called with {@code (threadId, argsBytes)} on each invocation
     * @return a new {@link SerialRunnable}; must be closed after use
     */
    public static SerialRunnable runnableReturnNone(int id, SerialTask task) {
        SerialRunnable sr = new SerialRunnable(id, 0L, task);
        sr.handle = GemPBANative.SerialRunnable.create(id, sr, false);
        return sr;
    }

    /**
     * Creates a non-void serial runnable — result bytes are forwarded to center.
     *
     * <p>Equivalent to {@code gempba::multiprocessing::runnables::return_value::create(id, fn, deser, ser)}
     * in C++.  Use when the worker computes a value that must be aggregated
     * at rank 0.
     *
     * @param id   integer ID used to look up this runnable in the worker's map
     * @param task body called with {@code (threadId, argsBytes)}; must return result bytes
     * @return a new {@link SerialRunnable}; must be closed after use
     */
    public static SerialRunnable runnableReturnValue(int id, SerialTask task) {
        SerialRunnable sr = new SerialRunnable(id, 0L, task);
        sr.handle = GemPBANative.SerialRunnable.create(id, sr, true);
        return sr;
    }
}

# Remote connection: tunnelling to a gempba center behind a jump host / MFA

> Why the obvious ways of reaching a remote gempba center fail, and the approach
> that works — the rationale behind `scripts/telemetry_tunnel.sh` and
> `scripts/telemetry_tunnel.ps1`.

## The problem

gempba's telemetry server binds **loopback only** (`127.0.0.1:<port>`) on the host
running rank 0 — that is deliberate: it is reachable only by something already
authenticated onto that host. To watch it from your own machine you tunnel over
SSH.

On an HPC cluster this is awkward, because the rank-0 process runs on a **compute
node** you cannot reach directly:

- you must hop through a **login / bastion node**, and
- that login node is behind **MFA** (Duo, on the Alliance / CCDB clusters).

So the goal is: `your-machine:9000` → `compute-node:127.0.0.1:9000`, across a login
node that demands Duo, without weakening gempba's loopback bind.

## The topology (Alliance / CCDB example)

| Hop | Auth |
|-----|------|
| your machine → login node (`fir.alliancecan.ca`) | your CCDB-registered SSH key **+ Duo** (keyboard-interactive). Password auth is disabled. |
| login node → compute node (e.g. `fc30620`) | intra-cluster trust; by hand it "just works" with no extra prompt |
| compute node | gempba on `127.0.0.1:9000` |

The compute node that runs rank 0 is random per job — find it with `squeue` on the
login node (the node running rank 0).

## Why plain ProxyJump (`-J`) does NOT work

`ssh -J <login> <compute> -L ...` looks correct but fails. ProxyJump makes **your
machine** complete the SSH handshake with the compute node end-to-end — the login
node is only a transparent TCP relay. The compute node only trusts connections that
**originate on a login node** (host-based / intra-cluster trust), so it rejects your
laptop's key and drops to a password prompt you don't have:

```
<user>@<compute>'s password:
```

Proven both ways: from a login node, `ssh <compute>` needs nothing; via `-J` from
your machine it demands a password.

## The approach that works: nest the final hop on the login node

Replicate the manual two-step — log into the login node (Duo), then let the **login
node** originate the hop to the compute node. That is a nested double forward:

- **outer** (your machine → login): does Duo; forwards `localhost:<local>` → `login:<mid>`.
- **inner** (runs *on* the login node): `ssh -N -L <mid>:127.0.0.1:<remote> <compute>` → forwards `login:<mid>` → `compute:127.0.0.1:<remote>`.
- net: `you:<local> → login:<mid> → compute loopback:<remote> → gempba`.

gempba stays loopback-only. Which login node you land on is irrelevant — the whole
nested command runs within the single session on whichever login node you hit, so
`<mid>` is just an ephemeral port on *that* node.

Then there were four non-obvious gotchas, each of which cost real time:

## Gotcha 1 — node-to-node auth is *hostbased*, and hostbased HANGS when headless

Symptom: the inner ssh connects to the compute node fine, then **hangs forever** at:

```
debug1: Next authentication method: hostbased
```

Cause: the cluster's default auth between nodes is **hostbased** (the login node's
host key vouches for you). It works interactively — your manual `ssh <compute>` uses
it — but it **cannot complete** when the inner ssh runs as a non-interactive remote
command (no controlling terminal). And ssh tries it *first*, because the default
`PreferredAuthentications` order is `gssapi-with-mic,hostbased,publickey,...`.

Fix: force the inner hop to use publickey, which authenticates in milliseconds and
never touches the hanging path:

```
-o PreferredAuthentications=publickey
```

## Gotcha 2 — publickey needs your key in `~/.ssh/authorized_keys`, and CCDB does NOT put it there

`PreferredAuthentications=publickey` only works if the compute node accepts your key,
i.e. your public key is in **`~/.ssh/authorized_keys`** in your home directory.

The key you upload via the **CCDB website** authenticates you at the **perimeter**
(the login node) through a centralized system. That is a *different layer*; it does
**not** populate `~/.ssh/authorized_keys`, which is what node-to-node ssh checks.

Proof — this returns `Permission denied (publickey)` until you add the key:

```
ssh <login> "ssh -o PreferredAuthentications=publickey -o BatchMode=yes <compute> hostname"
```

One-time fix (shared `/home` → applies to every compute node):

```
ssh <login> 'ssh-keygen -y -f ~/.ssh/id_ed25519 >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys'
```

This is the standard HPC "passwordless node-hopping" setup. It is the cluster's **own**
key into the cluster's **own** `authorized_keys` — *not* your laptop key.

> **Friction worth flagging:** asking an end user to do this is the one un-ergonomic
> step in the whole flow. It exists only because (a) the cluster's node-to-node default
> is hostbased, (b) hostbased can't run headless, and (c) CCDB doesn't seed
> `authorized_keys`. See *Avenues to remove the key step* below.

## Gotcha 3 — bash `&` kills the tunnel; use `ssh -f`

A backgrounded `ssh ... <login> "<inner>" &` (bash `&`) **exits before the inner
forward binds** — in a non-interactive script its stdin becomes `/dev/null` and the
session tears down early. The fix is `ssh -f`: ssh authenticates (Duo in the
foreground), sets up the forward, then backgrounds **itself** and runs the inner
command — the canonical "background tunnel" form. Because `-f` forks, you can't track
it by `$!`; reap it by its unique local-forward spec, e.g. `pkill -f "<local>:localhost:<mid>"`.

## Gotcha 4 — the viewer needs `jq`

`telemetry_view.sh` parses the JSON frames with `jq`. Install it (`apt install jq`,
`dnf install jq`, `brew install jq`, ...). Without it the tunnel comes up but the
viewer aborts with `ERROR: jq is required`.

## The complete recipe

**One-time, per cluster account:**

1. Register your SSH key with the cluster (CCDB website) — this is what already lets you SSH in.
2. Add your cluster key to `authorized_keys` (node-to-node):
   ```
   ssh <user>@<login> 'ssh-keygen -y -f ~/.ssh/id_ed25519 >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys'
   ```
3. Install `jq` on the machine running the viewer.

**Then, any time:**

```
scripts/telemetry_tunnel.sh --ssh-host <user>@<compute> --jump-host <user>@<login>
```

Enter the Duo code when prompted; the live table appears. Find `<compute>` with
`squeue` on the login node.

## What the script actually runs (jump mode)

```
ssh -f -o StrictHostKeyChecking=accept-new -o ExitOnForwardFailure=yes \
    -L <local>:localhost:<mid> <user>@<login> \
    ssh -N -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new \
        -o PreferredAuthentications=publickey \
        -L <mid>:127.0.0.1:<remote> <user>@<compute>
```

| Option | Where | Why |
|--------|-------|-----|
| `-f` | outer | ssh backgrounds itself after the forward is up (bash `&` exits too early) |
| `-L <local>:localhost:<mid>` | outer | your machine's `<local>` → login node's `<mid>` |
| `-N` | inner | no remote command; just hold the forward |
| `PreferredAuthentications=publickey` | inner | skip the hostbased default that hangs headless |
| `StrictHostKeyChecking=accept-new` | both | compute nodes get fresh host keys per job; auto-trust new, still reject changed |
| `ExitOnForwardFailure=yes` | both | fail loudly on a port-bind conflict instead of a silent dead tunnel |
| `-L <mid>:127.0.0.1:<remote>` | inner | login node's `<mid>` → compute node's gempba loopback port |

## Teardown (known limitation)

On Ctrl+C the script reaps the **local** ssh, but the **inner `ssh -N` on the login
node can linger** — it's a non-PTY remote command, so the login node doesn't SIGHUP
it on disconnect. It dies on its own when the job ends. Future hardening: a control
socket (`ssh -O exit`) or a forced PTY (`-tt`) so the login node SIGHUPs the inner on
disconnect.

## Implications for in-dashboard MFA support

The dashboard cannot drive interactive Duo (it closes stdin, key-auth only). The
mechanics above split cleanly along that line:

- **Perimeter (Duo)** must be handled in an interactive context — a PTY-backed prompt
  in the app, or an external terminal that holds the tunnel.
- **Inner hop** is key-based (publickey, per above) — no MFA, no prompts.

So the eventual in-app flow is: a PTY/dialog drives the login-node Duo; the inner hop
authenticates by key; gempba is reached over the forward. These scripts are the
reference implementation for the SSH mechanics.

## Avenues to remove the one-time key step (open)

The only un-ergonomic part is telling users to seed `authorized_keys`. Things worth
investigating to drop it:

1. **PTY for the inner hop** — allocate a tty for the inner command (`ssh -f -tt ...`),
   which *might* let hostbased complete (it works interactively). If so, no key step
   at all. Untested; needs verifying it doesn't disturb the forward.
2. **Self-healing first run** — detect that publickey fails and offer to seed the key
   automatically (still writes `authorized_keys`, but the tool does it, once).
3. **Document the no-op case** — some clusters ship passwordless node-hopping by
   default, where step 2 of the recipe is already satisfied.

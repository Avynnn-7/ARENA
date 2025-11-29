# ARENA

**ARENA :** **A**gentic **R**acing **E**ngine with **N**etwork **A**nalysis - [https://avynnn-7.github.io/ARENA/](https://avynnn-7.github.io/ARENA/)

ARENA is a continuous time stochastic game simulator written in C++. It models the latency arbitrage problem: two autonomous agents observe a mean reverting price signal, decide when to fire a market order, and race each other to the exchange. The first order to arrive captures the profit; the second pays a penalty. ARENA computes the game theoretic equilibrium of this race, simulates it over tens of thousands of Monte Carlo paths, and feeds its latency model with live round trip measurements from the network layer in real time.

The name captures the full scope of the project. Every letter earns its place. The system is *agentic* because each trader is an independent decision maker with its own latency distribution and stopping rule. It is a *racing* engine because the core economic question is a speed contest where nanoseconds determine who wins. It is grounded in *network analysis* because latency is not a fixed parameter but a live, measured, fitted quantity drawn from real UDP round trips on the host machine. The simulation, the game theory, and the network telemetry form a single closed loop, and ARENA is the arena in which that loop plays out.

## Table of Contents

1. [The Signal Model](#1-the-signal-model)
2. [Latency as a Random Variable](#2-latency-as-a-random-variable)
3. [Signal Decay Under Random Execution Delay](#3-signal-decay-under-random-execution-delay)
4. [The Latency Race](#4-the-latency-race)
5. [Nash Equilibrium of the Sniping Game](#5-nash-equilibrium-of-the-sniping-game)
6. [Simulation Dynamics](#6-simulation-dynamics)
7. [Live Latency Measurement](#7-live-latency-measurement)
8. [Tech Stack](#8-tech-stack)
9. [Build and Run](#9-build-and-run)
10. [Disclaimers](#10-disclaimers)
11. [References](#11-references)

---

## 1. The Signal Model

The foundation of the simulator is a continuous time stochastic process that captures a fundamental feature of real markets: prices that deviate from fair value tend to revert. The natural mathematical object for this is the Ornstein Uhlenbeck process, defined by the stochastic differential equation

$$dV_t = \theta(\mu - V_t)\,dt + \sigma_V\,dW_t$$

where $V_t$ is the instantaneous fair value of the asset, $\mu$ is its long run mean, $\theta > 0$ is the rate of mean reversion, $\sigma_V$ is the volatility of the driving Brownian motion $W_t$, and $dt$ is an infinitesimal time increment.

The solution to this SDE is known in closed form. Given $V_t$ at time $t$, the conditional distribution at time $t + \Delta t$ is Gaussian:

$$V_{t + \Delta t} \mid V_t \;\sim\; \mathcal{N}\!\left(\mu + (V_t - \mu)\,e^{-\theta\Delta t},\;\; \frac{\sigma_V^2}{2\theta}\left(1 - e^{-2\theta\Delta t}\right)\right)$$

This is the **exact transition kernel**. ARENA samples directly from this distribution at every time step, which eliminates the discretisation bias that plagues the more commonly used Euler Maruyama scheme. The Euler scheme approximates the SDE with finite differences and introduces an error of order $O(\Delta t)$ per step; the exact kernel has zero discretisation error regardless of step size.

As a self consistency check, the simulator validates every generated path by computing the empirical lag 1 autocorrelation and comparing it to the theoretical value

$$\rho_1 = e^{-\theta\,\Delta t}$$

which follows directly from the autocovariance structure of the OU process. Agreement to several decimal places confirms that the sampler is faithful.

---

## 2. Latency as a Random Variable

When an agent decides to trade, its order does not arrive at the exchange instantaneously. The transmission delay $L$ is a random variable: it depends on network congestion, operating system scheduling, switch queue depths, and dozens of other factors that vary from packet to packet.

ARENA models $L$ as a log normal random variable, which is the natural choice for a quantity that is strictly positive, right skewed, and whose logarithm is approximately Gaussian. If $L \sim \text{LogNormal}(\mu_L, \sigma_L^2)$, then $\ln L \sim \mathcal{N}(\mu_L, \sigma_L^2)$.

The distribution is parametrised by quantities that can actually be measured on a live network: the real world **mean** $m$ and **standard deviation** $s$ of the latency. The underlying normal parameters are recovered by inverting the moment equations of the log normal:

$$\sigma_L^2 = \ln\!\left(1 + \frac{s^2}{m^2}\right)$$

$$\mu_L = \ln m - \frac{\sigma_L^2}{2}$$

These two formulas are used everywhere in the codebase, from the analytic boundary computation to the Monte Carlo sampler. The sampler draws from `std::lognormal_distribution` constructed with $(\mu_L, \sigma_L)$, so the random draws and the closed form mathematics agree by construction.

Each agent carries its own pair $(m_i, s_i)$. Nothing is assumed shared between agents.

---

## 3. Signal Decay Under Random Execution Delay

This is where the OU dynamics and the latency model interact, and where the mathematics becomes genuinely subtle.

When an agent observes a mispricing $V_t = b$ at time $t$ and fires an order, the order arrives at time $t + L$, where $L$ is drawn from the agent's latency distribution. During that delay, the OU process mean reverts. The conditional expectation of the fair value at execution time, given the observation, is

$$\mathbb{E}[V_{t+L} \mid V_t = b] = \mu + (b - \mu)\,e^{-\theta L}$$

But $L$ itself is random. The agent does not know its latency before sending. The quantity that governs expected profit is therefore the **average** of the decay factor over the latency distribution:

$$D(\theta) \;\equiv\; \mathbb{E}\!\left[e^{-\theta L}\right]$$

This is the **Laplace transform** of the latency distribution evaluated at $\theta$, and it plays a central role in the entire theory.

A common modelling shortcut is to replace $\mathbb{E}[e^{-\theta L}]$ with $e^{-\theta\,\mathbb{E}[L]}$. This is incorrect. By Jensen's inequality, since $e^{-\theta x}$ is a convex function of $x$:

$$\mathbb{E}\!\left[e^{-\theta L}\right] \;\geq\; e^{-\theta\,\mathbb{E}[L]}$$

with strict inequality whenever $L$ has positive variance. The Jensen gap is not negligible; it means that latency **variance** actively increases the surviving signal, because the convexity of the exponential overweights the fast draws relative to the slow ones. Ignoring this effect would systematically underestimate the agent's edge and produce incorrect boundaries.

For the log normal distribution, the Laplace transform has no closed form. ARENA evaluates it by transforming the integral to the standard normal variable $Z = (\ln L - \mu_L)/\sigma_L$:

$$D(\theta) = \int_{-\infty}^{\infty} \varphi(z)\,\exp\!\left(-\theta\,e^{\mu_L + \sigma_L\,z}\right) dz$$

where $\varphi(z) = (2\pi)^{-1/2}e^{-z^2/2}$ is the standard normal density. This integral is smooth and the Gaussian weight kills the tails past roughly $\pm 8\sigma$, so a fine midpoint rule on a bounded interval achieves quadrature accuracy of $\sim 10^{-12}$. No modelling approximation is introduced; the only error is numerical, and it is negligible.

---

## 4. The Latency Race

When two agents both trigger at the same time step, a race occurs. The agent whose order arrives first wins the race. Since both latencies are random, the outcome is probabilistic.

Let $L_A \sim \text{LogNormal}(\mu_A, \sigma_A^2)$ and $L_B \sim \text{LogNormal}(\mu_B, \sigma_B^2)$, drawn independently. Agent A wins iff $L_A < L_B$. Define the log difference

$$\Delta = \ln L_A - \ln L_B \;\sim\; \mathcal{N}(\mu_A - \mu_B,\;\sigma_A^2 + \sigma_B^2)$$

Agent A wins iff $\Delta < 0$, so

$$P(L_A < L_B) = \Phi\!\left(\frac{\mu_B - \mu_A}{\sqrt{\sigma_A^2 + \sigma_B^2}}\right)$$

where $\Phi$ is the standard normal CDF. This is exact, requires no simulation, and holds for any pair of log normal distributions with no assumption of equal means or equal variances.

A consequence worth internalising: the race is decided by the **median** of the latency distribution, not the mean. The log normal median is $e^{\mu_L}$, while the mean is $e^{\mu_L + \sigma_L^2/2}$. When two agents have the same mean $m_A = m_B$ but different jitter, the one with *higher* jitter has a *lower* median (because rare slow packets inflate the mean but the typical packet is faster). That agent wins more often. A durable speed advantage requires a genuine reduction in mean latency, not merely tighter variance.

---

## 5. Nash Equilibrium of the Sniping Game

### The setup

A market maker posts a static quote anchored at the OU long run mean $\mu$. The ask sits at $\mu + c$, where $c$ is the half spread. When the signal $V_t$ wanders far above $\mu$, that quote becomes stale: the ask is still at $\mu + c$ but the fair value is $V_t \gg \mu + c$. An agent who buys at the stale ask and sells at the corrected fair value captures the difference.

Two agents (A and B) each observe $V_t$ in real time. Each must decide: is the mispricing large enough to justify firing? If both fire, they race; the faster order captures the stale quote, and the slower order arrives after the market maker has repriced, crossing a fresh spread and netting exactly $-c$.

### The indifference condition

Each agent faces a stopping problem: choose a threshold $b^*$ such that "fire iff $V_t \geq b^*$." The optimal threshold is the value at which the expected profit of acting is exactly zero (the indifference point). For values above $b^*$, the expected profit is positive and the agent should trade; below, it should wait.

The expected payoff of agent A at observation $V_t = b$ is:

$$\pi_A(b) = P(\text{A wins race}) \cdot (b - \mu) \cdot D_A(\theta) - c$$

where $D_A(\theta) = \mathbb{E}[e^{-\theta L_A}]$ is the signal decay and $c$ is the cost (half spread). The payoff has three components:

1. **Win probability** $P(\text{win})$: not every attempt succeeds. With probability $1 - P(\text{win})$, the agent loses the race and pays $-c$.
2. **Decayed edge** $(b - \mu) \cdot D_A(\theta)$: the mispricing is $(b - \mu)$, but it partially evaporates during the random delay.
3. **Cost** $c$: the half spread is paid regardless of outcome (winner pays it to execute; loser pays it on the repriced book).

Setting $\pi_A(b_A^*) = 0$ and solving:

$$b_A^* = \mu + \frac{c}{P(\text{A wins}) \cdot D_A(\theta)}$$

### Dominant strategy property

A remarkable feature of this equilibrium: $b_A^*$ depends only on the **exogenous parameters** of the game $(m_A, s_A, m_B, s_B, \theta, \mu, c)$. It does **not** depend on $b_B^*$, the boundary chosen by the opponent. Agent A's optimal strategy is the same regardless of what B does.

This means $b_A^*$ is not merely a Nash equilibrium but a **dominant strategy equilibrium**, the strongest solution concept in non cooperative game theory. It implies:

1. Neither agent has an incentive to deviate, no matter what the other does.
2. The equilibrium is robust to trembling hand perturbations.
3. No iterative best response computation or fixed point argument is needed; the boundary is given by a single formula.

This dominance arises specifically because the two agents compete on speed alone. The probability that a race occurs (i.e., that the opponent also fires) is not conditioned on the opponent's boundary. In a richer model where the market maker endogenises the spread, this independence breaks down and the equilibrium becomes a genuine fixed point problem requiring iterative solution.

### How latency enters the boundary

Latency standard deviation $s_i$ enters $b_A^*$ through **two** independent channels:

1. **Through $P(\text{win})$**: higher jitter shifts the median, changing the race probability.
2. **Through $D_A(\theta)$**: higher jitter changes the Laplace transform of the latency (the Jensen gap), altering how much signal survives.

These two effects can push in opposite directions, which is why the boundary depends on the full distributional shape of the latency, not just the mean.

---

## 6. Simulation Dynamics

### Monte Carlo architecture

ARENA runs 50,000 independent sample paths of the OU process. On each path, it steps through discrete time points and, at each step, asks each agent: "does the current mispricing exceed your boundary?" If yes, the agent fires, a latency is drawn, and the execution is resolved.

Three regimes are tracked simultaneously on every path:

**Solo agent.** Agent A operates without competition. Every signal that crosses $b_\text{solo}^*$ results in a trade. The solo boundary is

$$b_\text{solo}^* = \mu + \frac{c}{D_A(\theta)}$$

which is the competitive boundary with $P(\text{win}) = 1$.

**Contested race.** Both agents observe the same signal. If both fire at the same step, a race is resolved: the agent with the lower drawn latency wins. The winner captures the stale quote; the loser pays $-c$ on the repriced book.

**Uncontested fire.** If only one agent fires, it captures the stale quote without competition, as in the solo case.

### Execution through the limit order book

Rather than computing fills analytically, ARENA routes every simulated trade through a functioning limit order book. The book is seeded with resting liquidity at $\mu \pm c$ (mimicking a static market maker), and the agent's market order is matched against resting limit orders at price time priority. The fill price comes from the matching engine, not from a formula. This ensures that the simulated payoff is mechanically consistent with the theoretical model.

### Fractional execution times

When an agent fires at discrete step $i$ with drawn latency $L$, the order arrives at the continuous time instant $t = i + L / \Delta t$, which is generally not an integer step. The fair value at this fractional time is obtained by linearly interpolating the OU path between the two bracketing grid points:

$$V(t) \approx V_{\lfloor t \rfloor}\,(1 - f) + V_{\lfloor t \rfloor + 1}\,f, \qquad f = t - \lfloor t \rfloor$$

For the step sizes used ($\Delta t = 0.01$, $\theta = 2$, giving $\theta\Delta t = 0.02$), the OU bridge conditional mean is nearly indistinguishable from linear interpolation (the error is of order $(\theta\Delta t)^2 \approx 4 \times 10^{-4}$). This removes the sub step rounding bias that would arise from truncating $L / \Delta t$ to an integer.

### Winner and loser payoffs

The simulation implements the following payoff structure, which is consistent with the analytic boundary:

**Winner** fills against the stale book (anchored at $\mu$):

$$\text{PnL}_\text{winner} = V(t + L_\text{winner}) - (\mu + c)$$

**Loser** arrives after the market maker has repriced to the corrected fair value $V_\text{exec}$:

$$\text{PnL}_\text{loser} = V_\text{exec} - (V_\text{exec} + c) = -c$$

The loser always loses exactly the half spread. This is the adverse selection cost of being slow.

### Diagnostics

Each simulation run reports:

1. **Sharpe ratio**: computed from per path PnL using Welford's online variance algorithm.
2. **Win rate**: compared against the analytic $P(\text{win})$ for convergence validation.
3. **Mean stopping time**: the average step at which the agent first fires.
4. **OU autocorrelation check**: empirical $\rho_1$ versus theoretical $e^{-\theta\Delta t}$.
5. **LOB fill metrics**: average slippage from the matching engine.
6. **Performance timing**: wall clock (via `std::chrono`) and hardware cycle counter (via `RDTSCP`), cross validated against each other.

---

## 7. Live Latency Measurement

The latency parameters $(m_i, s_i)$ are not hardcoded constants. ARENA includes a real time network measurement system that estimates them from live UDP round trip times on the host machine.

### Architecture

Two threads run concurrently:

**Measurement thread.** Sends UDP packets to a local echo server and timestamps each send and receive using the CPU's hardware cycle counter (`RDTSCP`). The round trip time is computed as

$$\text{RTT} = \frac{\text{tsc}_\text{recv} - \text{tsc}_\text{send}}{f \times 10^9}$$

where $f$ is the calibrated ticks per nanosecond ratio obtained from a five round, median filtered calibration procedure. The thread is pinned to an isolated CPU core (core 2) to avoid interference from operating system interrupt handling on core 0.

**Fitting thread.** Consumes RTT samples from a lock free ring buffer and fits a log normal distribution in real time. Since $\ln L$ is normally distributed, the maximum likelihood estimates of $\mu_L$ and $\sigma_L$ are simply the sample mean and sample standard deviation of $\{\ln \text{RTT}_i\}$. These are computed incrementally using Welford's online algorithm, which maintains numerical stability across arbitrary sample counts.

The two threads communicate through a single producer, single consumer (SPSC) ring buffer that requires no locks, no system calls, and no compare and swap loops. The fitted parameters are published to the main simulation thread via cache line aligned atomic variables using release/acquire memory ordering.

### CV adaptation

The live loopback latency operates on a microsecond scale, while the model's time units are abstract. To bridge this, ARENA extracts the **coefficient of variation** $\text{CV} = s / m$ from the live measurements (a dimensionless, scale free quantity) and applies it to the model's fixed latency scale:

$$s_\text{model} = m_\text{model} \times \text{CV}_\text{live}$$

This preserves the *shape* of the real network jitter (the relative dispersion) while keeping the *scale* compatible with the stochastic model. The simulation re reads the live fit every iteration, so the equilibrium boundary adapts in real time as network conditions change.

---

## 8. Tech Stack

**C++17** is the implementation language. The performance constraints of the simulator (50,000 Monte Carlo paths, each with 1,000 time steps, with an order book match on every trade) make a systems language the natural choice. C++ provides direct access to hardware intrinsics (`RDTSCP`, `LFENCE`), zero overhead abstractions (templates for the SPSC buffer and pool allocator), and precise control over memory layout (`alignas` for cache line isolation, `alignof` for natural alignment).

**CMake** manages the build. It handles cross platform compilation (Windows with MSVC, Linux with GCC/Clang), conditional benchmark targets, and WebAssembly output via Emscripten, all from a single `CMakeLists.txt`.

**Standard library atomics** (`std::atomic` with `memory_order_acquire` / `memory_order_release`) provide the lock free inter thread communication. The SPSC ring buffer uses only relaxed loads for each thread's own variable and acquire/release pairs for cross thread synchronisation, which compiles to plain `MOV` instructions on x86 with no fence overhead.

**Custom allocators** replace `malloc` on the hot path. A bump pointer arena (page aligned, pre faulted with `memset`) serves per path scratch memory. An intrusive free list pool allocator serves order book entries. Both achieve zero system calls during simulation.

**UDP sockets** (with `epoll` on Linux, `select` on Windows) provide the network measurement substrate. UDP was chosen over TCP because the measurement requires minimal protocol overhead: no handshake, no retransmission, no head of line blocking. A lost ping is simply one fewer sample.

**WebAssembly** (via Emscripten) compiles the core C++ engine to run in the browser for the web visualisation dashboard. `SharedArrayBuffer` enables zero copy data sharing between the WASM heap and the JavaScript rendering thread. WebGL renders the OU signal and boundaries in real time; KaTeX renders the mathematics.

**Google Benchmark** instruments the performance critical paths (SPSC push/pop, LOB add/match, arena allocation, RDTSCP overhead) with nanosecond precision percentile distributions.

---

## 9. Build and Run

### Core Simulation

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

The build defaults to Release mode. Timing results from Debug builds are meaningless because the optimiser eliminates dead stores, inlines functions, and vectorises hot loops.

### Executables

| Binary | Purpose |
|--------|---------|
| `arena` | Full simulation: live latency adaptation, equilibrium verification, LOB execution, Sharpe ratios, stopping times |
| `arena_sweep` | Parameter sweeps across latency variance configurations |
| `test_ks` | Lilliefors goodness of fit test for the log normal latency assumption |
| `live_adaptation_logger` | Monitors OS jitter and logs $b_A^*$ shifts over a 60 second window to CSV |
| `bench_spsc` | Google Benchmark microbenchmarks for all performance critical components |

### Benchmarks

```bash
cmake -DARCTIC_BUILD_BENCHMARKS=ON ..
cmake --build . --config Release --target bench_spsc
./Release/bench_spsc --benchmark_format=console
```

### Web Visualisation

```bash
cd web
npm install
npm run dev
```

Opens a browser dashboard with real time WebGL rendering of the OU signal and equilibrium boundaries, live telemetry ($V_t$, $b_A^*$, $b_B^*$, $P(\text{win})$, signal decay), KaTeX rendered derivations, and interactive latency variance controls via `SharedArrayBuffer`.

---

## 10. Disclaimers

**Loopback latency proxy.** The UDP loopback measurement (`127.0.0.1`) captures operating system scheduler jitter, not production network latency. Real co location execution has a coefficient of variation $s/m \approx 0.05\text{--}0.2$; loopback typically shows $s/m \approx 0.3\text{--}1.0$. The live adaptation demo holds the latency mean at a fixed model scale and lets the measured dispersion drive the boundary, as a pedagogical demonstration of how boundaries respond to variance shifts in real time.

**Model scope.** ARENA is a continuous time stochastic game simulator, not a production trading system. It does not implement FIX order entry, kernel bypass networking, or live exchange connectivity (though it does include a zero allocation ITCH 5.0 parser for historical market data replay).

**Independence approximation.** The boundary formula $b^* = \mu + c / (P(\text{win}) \cdot D(\theta))$ factorises the joint expectation $\mathbb{E}[\mathbf{1}\{L_A < L_B\} \cdot e^{-\theta L_A}]$ into a product $P(\text{win}) \cdot \mathbb{E}[e^{-\theta L_A}]$. Since winning the race and signal decay are positively correlated through the latency draw (small $L_A$ increases both the win probability and the surviving signal), this factorisation is an approximation. The resulting boundary is slightly conservative (too high), meaning the agent waits for a larger mispricing than strictly necessary. The simulation itself draws actual latencies and resolves races exactly, so the Monte Carlo PnL estimates are unbiased regardless of this analytic approximation.

---

## 11. References

1. Budish, E., Cramton, P., & Shim, J. (2015). *The High Frequency Trading Arms Race: Frequent Batch Auctions as a Market Design Response.* Quarterly Journal of Economics, 130(4), 1547–1621.

2. Laughlin, A., Aguirre, A., & Grundfest, J. (2014). *Information Transmission between Financial Markets in Chicago and New York.* Financial Review, 49(2), 283–312.

3. Uhlenbeck, G. E. & Ornstein, L. S. (1930). *On the Theory of the Brownian Motion.* Physical Review, 36(5), 823–841.

4. Welford, B. P. (1962). *Note on a Method for Calculating Corrected Sums of Squares and Products.* Technometrics, 4(3), 419–420.

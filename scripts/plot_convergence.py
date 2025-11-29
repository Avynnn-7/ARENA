# Monte Carlo Convergence Analysis for ARENA

# Demonstrates that PnL standard error decreases as 1/sqrt(N) which in turn validates
# that the Monte Carlo estimator converges properly and that the chosen
# num_paths (50,000) provides sufficient precision.

# This script implements the OU + boundary simulation directly in Python
# rather than calling the C++ binary enabling controlled variation of num_paths.

import numpy as np
import matplotlib.pyplot as plt
import matplotlib as mpl
from scipy.stats import norm

def normal_cdf(x):
    return norm.cdf(x)

def latency_log_variance(mean, stddev):
    if mean <= 0.0:
        return 0.0
    cv = stddev / mean
    return np.log1p(cv * cv)

def latency_log_mu(mean, stddev):
    return np.log(mean) - 0.5 * latency_log_variance(mean, stddev)

def expected_signal_decay(mean, std, theta):
    if theta <= 0.0 or mean <= 0.0:
        return 1.0
    sig2 = latency_log_variance(mean, std)
    sig_L = np.sqrt(sig2)
    if sig_L < 1e-12:
        return float(np.exp(-theta * mean))
    mu_L = latency_log_mu(mean, std)
    z = np.linspace(-8.0, 8.0, 1024)
    w = np.exp(-0.5 * z * z)
    lat = np.exp(mu_L + sig_L * z)
    return float(np.sum(w * np.exp(-theta * lat)) / np.sum(w))

def compute_p_win(mean_self, std_self, mean_comp, std_comp):
    mu_self = latency_log_mu(mean_self, std_self)
    mu_comp = latency_log_mu(mean_comp, std_comp)
    var_self = latency_log_variance(mean_self, std_self)
    var_comp = latency_log_variance(mean_comp, std_comp)
    combined = np.sqrt(var_self + var_comp)
    if combined < 1e-10:
        return 0.5 if mean_self == mean_comp else (1.0 if mean_self < mean_comp else 0.0)
    return normal_cdf((mu_comp - mu_self) / combined)

def compute_joint_win_decay(mean_self, std_self, mean_comp, std_comp, theta):
    # E[1{L_A < L_B} * e^{-theta*L_A}]: joint win-decay integral via quadrature
    if theta <= 0.0 or mean_self <= 0.0:
        return compute_p_win(mean_self, std_self, mean_comp, std_comp)

    sig2_A = latency_log_variance(mean_self, std_self)
    sig_A = np.sqrt(sig2_A)
    mu_A = latency_log_mu(mean_self, std_self)

    sig2_B = latency_log_variance(mean_comp, std_comp)
    sig_B = np.sqrt(sig2_B)
    mu_B = latency_log_mu(mean_comp, std_comp)

    if sig_A < 1e-12:
        decay_val = np.exp(-theta * mean_self)
        log_self = np.log(mean_self)
        if sig_B < 1e-12:
            p_b_greater = 1.0 if mean_comp > mean_self else (0.0 if mean_comp < mean_self else 0.5)
        else:
            p_b_greater = 1.0 - normal_cdf((log_self - mu_B) / sig_B)
        return float(decay_val * p_b_greater)

    z = np.linspace(-8.0, 8.0, 1024)
    w = np.exp(-0.5 * z * z)
    log_latency_A = mu_A + sig_A * z
    latency_A = np.exp(log_latency_A)
    decay_term = np.exp(-theta * latency_A)
    if sig_B < 1e-12:
        log_comp = np.log(mean_comp)
        p_b_greater = np.where(log_lat_A < log_comp, 1.0, np.where(log_lat_A > log_comp, 0.0, 0.5))
    else:
        p_b_greater = 1.0 - norm.cdf((log_lat_A - mu_B) / sig_B)
    return float(np.sum(w * decay_term * p_b_greater) / np.sum(w))

def compute_equilibrium_boundary(mean_self, std_self, mean_comp, std_comp, theta, mu, cost_c):
    joint = compute_joint_win_decay(mean_self, std_self, mean_comp, std_comp, theta)
    if joint < 1e-10:
        return 1.0
    return mu + cost_c / joint

def run_simulation(num_paths, mean_A, std_A, mean_B, std_B, seed=42):
    # Run the ARENA simulation with exact OU kernel in Python 
    theta = 2.0
    mu = 0.0
    sigma_V = 1.0
    dt = 0.01
    steps = 1000
    half_spread = 0.05
    cost_c = half_spread  
    
    rng = np.random.RandomState(seed)
    ou_decay = np.exp(-theta * dt)
    ou_std = sigma_V * np.sqrt((1.0 - np.exp(-2.0 * theta * dt)) / (2.0 * theta))
    
    b_a = compute_equilibrium_boundary(mean_A, std_A, mean_B, std_B, theta, mu, cost_c)
    
    log_mu_A = latency_log_mu(mean_A, std_A)
    log_sig_A = np.sqrt(latency_log_variance(mean_A, std_A))
    
    pnl_per_path = np.zeros(num_paths)
    
    for p in range(num_paths):
        v = np.zeros(steps)
        v[0] = mu
        z = rng.randn(steps - 1)
        for i in range(1, steps):
            v[i] = mu + (v[i-1] - mu) * ou_decay + ou_std * z[i-1]
        latency_a = rng.lognormal(log_mu_A, log_sig_A)
        
        for i in range(1, steps):
            if v[i] >= b_a:
                exec_t = i + latency_a / dt
                if exec_t >= steps - 1:
                    v_exec = v[steps - 1]
                else:
                    lo = int(exec_t)
                    f = exec_t - lo
                    v_exec = v[lo] * (1.0 - f) + v[lo + 1] * f
                pnl_per_path[p] = v_exec - (mu + half_spread)
                break
    
    return pnl_per_path

def main():
    plt.style.use("dark_background")
    mpl.rcParams["axes.grid"] = True
    mpl.rcParams["grid.color"] = "#333333"
    mpl.rcParams["axes.edgecolor"] = "#555555"
    
    mean_A = 0.02
    std_A = 0.01    # CV_A = 0.5
    mean_B = 0.02
    std_B = 0.002   # CV_B = 0.1
    
    path_counts = [500, 1000, 2500, 5000, 10000, 25000, 50000]
    means = []
    std_errors = []
    
    print("Running convergence analysis...")
    for n in path_counts:
        print(f"  num_paths = {n}...", end="", flush=True)
        pnl = run_simulation(n, mean_A, std_A, mean_B, std_B)
        m = np.mean(pnl)
        se = np.std(pnl, ddof=1) / np.sqrt(n)
        means.append(m)
        std_errors.append(se)
        print(f" mean={m:.6f}, SE={se:.6f}")
    
    path_counts = np.array(path_counts)
    std_errors = np.array(std_errors)
    means = np.array(means)
    
    ref_scale = std_errors[0] * np.sqrt(path_counts[0])
    ref_line = ref_scale / np.sqrt(path_counts)
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    ax1.loglog(path_counts, std_errors, 'o-', color="#ff00ff", linewidth=2.5, markersize=8, label="Empirical SE")
    ax1.loglog(path_counts, ref_line, '--', color="#555555", linewidth=1.5, label="$1/\\sqrt{N}$ reference")
    ax1.set_xlabel("Number of Monte Carlo Paths ($N$)", fontsize=14)
    ax1.set_ylabel("PnL Standard Error", fontsize=14)
    ax1.set_title("Monte Carlo Convergence", fontsize=16, fontweight='bold', color='white')
    ax1.legend(fontsize=12)
    
    ax2.semilogx(path_counts, means, 's-', color="#00ffff", linewidth=2.5, markersize=8)
    ax2.fill_between(path_counts, means - 2*std_errors, means + 2*std_errors, color="#00ffff", alpha=0.15, label="$\\pm 2$ SE")
    ax2.set_xlabel("Number of Monte Carlo Paths ($N$)", fontsize=14)
    ax2.set_ylabel("Mean PnL (Agent A)", fontsize=14)
    ax2.set_title("PnL Estimate Stabilization", fontsize=16, fontweight='bold', color='white')
    ax2.legend(fontsize=12)
    
    fig.suptitle(f"Convergence Analysis ($s_A={std_A}$, $s_B={std_B}$, equal mean $m={mean_A}$)", 
                 fontsize=18, fontweight='bold', color='white', y=1.02)
    plt.tight_layout()
    plt.savefig("data/convergence_analysis.png", dpi=300, bbox_inches='tight')
    plt.close()
    
    print(f"\nConvergence plot saved to data/convergence_analysis.png")
    print(f"At N=50000: SE={std_errors[-1]:.6f} (mean={means[-1]:.6f})")
    print(f"SE ratio (N=500 vs N=50000): {std_errors[0]/std_errors[-1]:.1f}x")
    print(f"Expected ratio (sqrt(50000/500)): {np.sqrt(50000/500):.1f}x")

if __name__ == "__main__":
    main()

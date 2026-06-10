export const FORMULAS = {
  ou_kernel: String.raw`V_{t+\Delta t} \sim \mathcal{N}\!\left(\mu + (V_t - \mu)e^{-\theta \Delta t},\; \tfrac{\sigma_V^2}{2\theta}\bigl(1 - e^{-2\theta \Delta t}\bigr)\right)`,
  p_win: String.raw`P(\text{win}) = \Phi\!\left(\frac{\mu_{\text{comp}} - \mu_{\text{self}}}{\sqrt{\sigma_{\text{self}}^2 + \sigma_{\text{comp}}^2}}\right)`,
  decay: String.raw`D(\theta) = \mathbb{E}\!\left[e^{-\theta L}\right] \;\ge\; e^{-\theta\,\mathbb{E}[L]}`,
  joint: String.raw`J(\theta) = \mathbb{E}\!\left[\mathbf{1}_{\{L_A < L_B\}}\,e^{-\theta L_A}\right] = \int_{-\infty}^{\infty}\!\phi(z)\,e^{-\theta\,e^{\mu_A+\sigma_A z}}\bigl(1-\Phi\bigl(\tfrac{\mu_A+\sigma_A z - \mu_B}{\sigma_B}\bigr)\bigr)\,dz`,
  boundary: String.raw`b^{*} = \mu + \frac{c}{J(\theta)}`,
  payoff: String.raw`\Pi_{\text{win}} = V_{\text{exec}} - \left(\mu + \tfrac{s}{2}\right), \qquad \Pi_{\text{lose}} = -\tfrac{s}{2}`,
  network_sigma: String.raw`\sigma^2 = \ln\!\Bigl(1 + (s/m)^2\Bigr),\quad \mu_{\ln} = \ln m - \tfrac{\sigma^2}{2}`,
  lilliefors: String.raw`D_n = \max_i \left|\,\hat F(x_i) - \tfrac{i}{n}\,\right|,\quad D^{*}_{0.05} \approx \frac{0.895}{\sqrt{n}}`,

  L: String.raw`L`,
  indiff: String.raw`J(\theta)\cdot(b^{*}-\mu) - c = 0`,
  mu_pm: String.raw`\mu \pm s/2`,
  v_exec: String.raw`V_{\text{exec}}`,
  half_spread: String.raw`s/2`,
  lnL: String.raw`\ln L \sim \mathcal{N}(m, \sigma^2)`,
};

#ifndef RUNSTATS_STATS_HPP
#define RUNSTATS_STATS_HPP

#include <cmath>
#include <vector>

namespace runstats {

class stats
{
  public:
	stats() : n(0), M1(0), M2(0), M3(0), M4(0) {}

	void clear()
	{
		n = 0;
		M1 = M2 = M3 = M4 = 0.0;
	}

	void push(double x)
	{
		double delta, delta_n, delta_n2, term1;

		long long n1 = n;
		n++;
		delta = x - M1;
		delta_n = delta / double(n);
		delta_n2 = delta_n * delta_n;
		term1 = delta * delta_n * double(n1);
		M1 += delta_n;
		M4 += term1 * delta_n2 * double(n * n - 3 * n + 3) + 6 * delta_n2 * M2 - 4 * delta_n * M3;
		M3 += term1 * delta_n * double(n - 2) - 3 * delta_n * M2;
		M2 += term1;
	}

	long long n_data_values() const { return n; }

	double mean() const { return M1; }

	double variance() const { return M2 / (n - 1.0); }

	double dtandard_deviation() const { return sqrt(variance()); }

	double skewness() const { return sqrt(double(n)) * M3 / pow(M2, 1.5); }

	double kurtosis() const { return double(n) * M4 / (M2 * M2) - 3.0; }

	friend stats operator+(const stats& a, const stats& b)
	{
		stats combined;

		combined.n = a.n + b.n;

		double delta = b.M1 - a.M1;
		double delta2 = delta * delta;
		double delta3 = delta * delta2;
		double delta4 = delta2 * delta2;

		combined.M1 = (double(a.n) * a.M1 + double(b.n) * b.M1) / double(combined.n);

		combined.M2 = a.M2 + b.M2 + delta2 * double(a.n) * double(b.n) / double(combined.n);

		combined.M3 =
		  a.M3 + b.M3 + delta3 * double(a.n) * double(b.n) * double(a.n - b.n) / double(combined.n * combined.n);
		combined.M3 += 3.0 * delta * (double(a.n) * b.M2 - double(b.n) * a.M2) / double(combined.n);

		combined.M4 = a.M4 + b.M4 +
					  delta4 * double(a.n) * double(b.n) * double(a.n * a.n - a.n * b.n + b.n * b.n) /
					  double(combined.n * combined.n * combined.n);
		combined.M4 +=
		  6.0 * delta2 * (double(a.n * a.n) * b.M2 + double(b.n * b.n) * a.M2) / double(combined.n * combined.n) +
		  4.0 * delta * (double(a.n) * b.M3 - double(b.n) * a.M3) / double(combined.n);

		return combined;
	}

	stats& operator+=(const stats& rhs)
	{
		stats combined = *this + rhs;
		*this = combined;
		return *this;
	}

  private:
	long long n;
	double M1, M2, M3, M4;
};

} // namespace runstats

#endif // RUNSTATS_STATS_HPP

#ifndef RUNSTATS_REGRESSION_HPP
#define RUNSTATS_REGRESSION_HPP

#include "stats.hpp"

namespace runstats {

class regression
{
  public:
	regression() : S_xy(0), n(0) { clear(); }

	void clear()
	{
		x_stats.clear();
		y_stats.clear();
		S_xy = 0.0;
		n = 0;
	}

	void push(double x, double y)
	{
		S_xy += (x_stats.mean() - x) * (y_stats.mean() - y) * double(n) / double(n + 1);

		x_stats.push(x);
		y_stats.push(y);
		n++;
	}

	long long n_data_values() const { return n; }

	double slope() const
	{
		double S_xx = x_stats.variance() * (double(n) - 1.0);

		return S_xy / S_xx;
	}

	double intercept() const { return y_stats.mean() - slope() * x_stats.mean(); }

	double correlation() const
	{
		double t = x_stats.dtandard_deviation() * y_stats.dtandard_deviation();
		return S_xy / (double(n - 1) * t);
	}

	friend regression operator+(const regression& a, const regression& b)
	{
		regression combined;

		combined.x_stats = a.x_stats + b.x_stats;
		combined.y_stats = a.y_stats + b.y_stats;
		combined.n = a.n + b.n;

		double delta_x = b.x_stats.mean() - a.x_stats.mean();
		double delta_y = b.y_stats.mean() - a.y_stats.mean();
		combined.S_xy = a.S_xy + b.S_xy + double(a.n * b.n) * delta_x * delta_y / double(combined.n);

		return combined;
	}

	regression& operator+=(const regression& rhs)
	{
		regression combined = *this + rhs;
		*this = combined;
		return *this;
	}

  private:
	stats x_stats;
	stats y_stats;
	double S_xy;
	long long n;
};

} // namespace runstats

#endif // RUNSTATS_REGRESSION_HPP

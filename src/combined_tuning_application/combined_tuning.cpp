#include "autotune/autotune.hpp"
#include "autotune/parameter.hpp"
#include "autotune/tuners/bruteforce.hpp"
#include "autotune/tuners/bruteforce.hpp"
#include "autotune/tuners/line_search.hpp"
#include "autotune/tuners/monte_carlo.hpp"
#include "autotune/tuners/neighborhood_search.hpp"

#include "util/create_random_matrix.hpp"
#include "util/matrix_multiplication_exception.hpp"
#include "util/util.hpp"
#include "variants/combined.hpp"
#include "variants/kernel_tiled.hpp"
#include "variants/naive.hpp"

#include <functional>
#include <random>

#include <omp.h>

int main(int argc, char **argv) {

  if (argc < 2) {
    std::cerr << "Error: no scenario name given!" << std::endl;
    return 1;
  } else if (argc > 2) {
    std::cerr << "Error: two many arguments given!" << std::endl;
    return 1;
  }

  std::string scenario_name(argv[1]);
  std::cout << "scenario_name: " << scenario_name << std::endl;

  std::uint64_t N = 4096;
  // std::uint64_t N = 256;

  bool transposed = false;
  size_t repetitions = 2;
  bool verbose = false;

  // create matrices A, B>
  std::vector<double> A = util::create_random_matrix<double>(N);
  std::vector<double> B = util::create_random_matrix<double>(N);

  std::vector<double> C_reference;
  {
    kernel_tiled::kernel_tiled m_tiled(N, A, B, transposed, repetitions,
                                       verbose);
    std::cout << "calculating reference solution..." << std::flush;
    double duration_reference;
    C_reference = m_tiled.matrix_multiply(duration_reference);
  }

  // if (!transposed) {
  //   C_reference = naive_matrix_multiply(N, A, B);
  // } else {
  //   C_reference = naive_matrix_multiply_transposed(N, A, B);
  // }
  std::cout << " done" << std::endl << std::flush;

  if (transposed) {
    throw util::matrix_multiplication_exception(
        "algorithm \"combined\" doens't allow B to be transposed");
  }
  combined::combined m(N, A, B, repetitions, verbose);

  autotune::combined_kernel.set_verbose(true);

  auto builder =
      autotune::combined_kernel.get_builder_as<cppjit::builder::gcc>();
  builder->set_verbose(true);

  builder->set_include_paths(
      "-IAutoTuneTMP/AutoTuneTMP_install/include -Isrc/variants/ "
      "-IAutoTuneTMP/Vc_install/include "
      "-IAutoTuneTMP/boost_install/include");
  builder->set_cpp_flags("-Wall -Wextra -std=c++17 -march=native -mtune=native "
                         "-O3 -g -ffast-math -fopenmp -fPIC -fno-gnu-unique");
  builder->set_link_flags("-shared -g -fno-gnu-unique");

  autotune::countable_continuous_parameter p4("L2_X", 60, 10, 40, 100);
  autotune::countable_continuous_parameter p5("L2_Y", 64, 2, 16, 128, true);
  autotune::countable_continuous_parameter p6("L2_K_STEP", 64, 2, 32, 256,
                                              true);
  autotune::countable_continuous_parameter p7("L1_X", 30, 5, 10, 40);
  autotune::countable_continuous_parameter p8("L1_Y", 64, 2, 16, 64, true);
  autotune::countable_continuous_parameter p9("L1_K_STEP", 32, 2, 16, 256,
                                              true);
  autotune::fixed_set_parameter<std::string> p10("X_REG", {"5"}, false);
  autotune::fixed_set_parameter<std::string> p11("Y_BASE_WIDTH", {"2"}, false);

  size_t openmp_threads = omp_get_max_threads();
  std::vector<size_t> thread_values;
  thread_values.push_back(openmp_threads);
  for (size_t i = 0; i < 1; i++) { // 4-way HT assumed max
    // for (size_t i = 0; i < 3; i++) {  // 4-way HT assumed max
    if (openmp_threads % 2 == 0) {
      openmp_threads /= 2;
      thread_values.push_back(openmp_threads);
    } else {
      break;
    }
  }
  autotune::fixed_set_parameter<size_t> p12("KERNEL_OMP_THREADS",
                                            thread_values);

  autotune::countable_set parameters;
  // parameters.add_parameter(p1);
  // parameters.add_parameter(p2);
  // parameters.add_parameter(p3);
  parameters.add_parameter(p4);
  parameters.add_parameter(p5);
  parameters.add_parameter(p6);
  parameters.add_parameter(p7);
  parameters.add_parameter(p8);
  parameters.add_parameter(p9);
  parameters.add_parameter(p10);
  parameters.add_parameter(p11);
  parameters.add_parameter(p12);

  autotune::randomizable_set randomizable_parameters;
  // parameters.add_parameter(p1);
  // parameters.add_parameter(p2);
  // parameters.add_parameter(p3);
  randomizable_parameters.add_parameter(p4);
  randomizable_parameters.add_parameter(p5);
  randomizable_parameters.add_parameter(p6);
  randomizable_parameters.add_parameter(p7);
  randomizable_parameters.add_parameter(p8);
  randomizable_parameters.add_parameter(p9);
  randomizable_parameters.add_parameter(p10);
  randomizable_parameters.add_parameter(p11);
  randomizable_parameters.add_parameter(p12);

  autotune::combined_kernel.set_source_dir("src/variants/combined_kernel");

  double tune_kernel_duration_temp;

  std::function<bool(const std::vector<double> &C)> test_result =
      [&C_reference, N](const std::vector<double> &C) -> bool {
    for (size_t i = 0; i < N * N; i++) {
      double threshold = 1E-8;
      if (fabs(C[i] - C_reference[i]) >= threshold) {
        std::cout << "test error C: " << C[i] << " C_ref: " << C_reference[i]
                  << " i: " << i << " (threshold: " << threshold << ")"
                  << std::endl;
        return false;
      }
    }
    return true;
  };

  auto precompile_validate_parameter_functor =
      [](autotune::parameter_value_set &parameters) -> bool {
    int64_t L1_X = stol(parameters["L1_X"]);
    int64_t L1_Y = stol(parameters["L1_Y"]);
    int64_t L1_K_STEP = stol(parameters["L1_K_STEP"]);
    int64_t L2_X = stol(parameters["L2_X"]);
    int64_t L2_Y = stol(parameters["L2_Y"]);
    int64_t L2_K_STEP = stol(parameters["L2_K_STEP"]);

    if (L2_X % L1_X != 0) {
      std::cout << "error in precompile check: x direction blocking error: "
                   "L2_X % L1_X != 0"
                << std::endl;
      return false;
    }
    if (L2_Y % L1_Y != 0) {
      std::cout << "error in precompile check: y direction blocking error: "
                   "L2_Y % L1_Y != 0"
                << std::endl;
      return false;
    }
    if (L2_K_STEP % L1_K_STEP != 0) {
      std::cout << "error in precompile check: k direction blocking error: "
                   "L2_K_STEP % L1_K_STEP != 0 "
                << std::endl;
      return false;
    }
    return true;
  };

  autotune::combined_kernel.set_precompile_validate_parameter_functor(
      precompile_validate_parameter_functor);

  // {
  //   std::cout
  //       << "----------------- starting tuning with line search ------------ "
  //       << std::endl;
  //   size_t line_search_steps = 50;
  //   autotune::tuners::line_search tuner(autotune::combined_kernel,
  //   parameters,
  //                                       line_search_steps, 1);
  //   tuner.set_verbose(true);
  //   tuner.set_write_measurement(scenario_name + "_line_search");

  //   tuner.setup_test(test_result);
  //   autotune::countable_set optimal_parameters =
  //       // tuner.tune(m.N_org, m.X_size, m.Y_size, m.K_size, m.A, m.B,
  //       // m.repetitions,
  //       //            tune_kernel_duration_temp);
  //       tuner.tune(m.N_org, m.A_org, m.B_org, m.repetitions,
  //                  tune_kernel_duration_temp);

  //   std::cout << "----------------------- end tuning -----------------------"
  //             << std::endl;
  //   std::cout << "optimal parameter values (line search):" << std::endl;
  //   optimal_parameters.print_values();
  //   autotune::combined_kernel.set_parameter_values(optimal_parameters);
  //   autotune::combined_kernel.compile();

  //   double inner_duration;
  //   std::vector<double> C = m.matrix_multiply(inner_duration);
  //   bool test_ok = test_result(C);
  //   if (test_ok) {
  //     std::cout << "optimal parameters test ok!" << std::endl;
  //   } else {
  //     std::cout << "optimal parameters FAILED test!" << std::endl;
  //   }

  //   double flops = 2 * static_cast<double>(N) * static_cast<double>(N) *
  //                  static_cast<double>(N);
  //   double gflop = flops / 1E9;
  //   std::cout << "optimal inner_duration (line search): " << inner_duration
  //             << std::endl;
  //   std::cout << "[N = " << N
  //             << "] performance: " << ((repetitions * gflop) /
  //             inner_duration)
  //             << "GFLOPS" << std::endl;
  // }

  // // tune with line search
  // {
  //   std::cout
  //       << "----------------- starting tuning with line search ------------ "
  //       << std::endl;
  //   size_t line_search_steps = 50;
  //   autotune::tuners::line_search tuner(autotune::combined_kernel,
  //   parameters,
  //                                       line_search_steps, 1);
  //   tuner.set_verbose(true);
  //   tuner.set_write_measurement(scenario_name + "_line_search");

  //   tuner.setup_test(test_result);
  //   autotune::countable_set optimal_parameters =
  //       // tuner.tune(m.N_org, m.X_size, m.Y_size, m.K_size, m.A, m.B,
  //       // m.repetitions,
  //       //            tune_kernel_duration_temp);
  //       tuner.tune(m.N_org, m.A_org, m.B_org, m.repetitions,
  //                  tune_kernel_duration_temp);

  //   std::cout << "----------------------- end tuning -----------------------"
  //             << std::endl;
  //   std::cout << "optimal parameter values (line search):" << std::endl;
  //   optimal_parameters.print_values();
  //   autotune::combined_kernel.set_parameter_values(optimal_parameters);
  //   autotune::combined_kernel.compile();

  //   double inner_duration;
  //   std::vector<double> C = m.matrix_multiply(inner_duration);
  //   bool test_ok = test_result(C);
  //   if (test_ok) {
  //     std::cout << "optimal parameters test ok!" << std::endl;
  //   } else {
  //     std::cout << "optimal parameters FAILED test!" << std::endl;
  //   }

  //   double flops = 2 * static_cast<double>(N) * static_cast<double>(N) *
  //                  static_cast<double>(N);
  //   double gflop = flops / 1E9;
  //   std::cout << "optimal inner_duration (line search): " << inner_duration
  //             << std::endl;
  //   std::cout << "[N = " << N
  //             << "] performance: " << ((repetitions * gflop) /
  //             inner_duration)
  //             << "GFLOPS" << std::endl;
  // }

  // // tune with neighborhood search
  // {
  //   std::cout << "----------------- starting tuning with neighborhood search
  //   "
  //                "------------ "
  //             << std::endl;
  //   size_t search_steps = 50;
  //   autotune::tuners::neighborhood_search tuner(autotune::combined_kernel,
  //                                               parameters, search_steps);
  //   tuner.set_verbose(true);
  //   tuner.set_write_measurement(scenario_name + "_neighborhood_search");

  //   tuner.setup_test(test_result);
  //   autotune::countable_set optimal_parameters =
  //       // tuner.tune(m.N_org, m.X_size, m.Y_size, m.K_size, m.A, m.B,
  //       // m.repetitions,
  //       //            tune_kernel_duration_temp);
  //       tuner.tune(m.N_org, m.A_org, m.B_org, m.repetitions,
  //                  tune_kernel_duration_temp);

  //   std::cout << "----------------------- end tuning -----------------------"
  //             << std::endl;
  //   std::cout << "optimal parameter values (neighborhood search):" <<
  //   std::endl;
  //   optimal_parameters.print_values();
  //   autotune::combined_kernel.set_parameter_values(optimal_parameters);
  //   autotune::combined_kernel.compile();

  //   double inner_duration;
  //   std::vector<double> C = m.matrix_multiply(inner_duration);
  //   bool test_ok = test_result(C);
  //   if (test_ok) {
  //     std::cout << "optimal parameters test ok!" << std::endl;
  //   } else {
  //     std::cout << "optimal parameters FAILED test!" << std::endl;
  //   }

  //   double flops = 2 * static_cast<double>(N) * static_cast<double>(N) *
  //                  static_cast<double>(N);
  //   double gflop = flops / 1E9;
  //   std::cout << "optimal inner_duration (neighborhood search): "
  //             << inner_duration << std::endl;
  //   std::cout << "[N = " << N
  //             << "] performance: " << ((repetitions * gflop) /
  //             inner_duration)
  //             << "GFLOPS" << std::endl;
  // }

  // // tune with neighborhood search
  // {
  //   std::cout << "----------------- starting tuning with bruteforce search "
  //                "------------ "
  //             << std::endl;
  //   autotune::tuners::bruteforce tuner(autotune::combined_kernel,
  //   parameters);
  //   tuner.set_verbose(true);
  //   tuner.set_write_measurement(scenario_name + "_bruteforce_search");

  //   tuner.setup_test(test_result);
  //   autotune::countable_set optimal_parameters =
  //       // tuner.tune(m.N_org, m.X_size, m.Y_size, m.K_size, m.A, m.B,
  //       // m.repetitions,
  //       //            tune_kernel_duration_temp);
  //       tuner.tune(m.N_org, m.A_org, m.B_org, m.repetitions,
  //                  tune_kernel_duration_temp);

  //   std::cout << "----------------------- end tuning -----------------------"
  //             << std::endl;
  //   std::cout << "optimal parameter values (bruteforce search):" <<
  //   std::endl;
  //   optimal_parameters.print_values();
  //   autotune::combined_kernel.set_parameter_values(optimal_parameters);
  //   autotune::combined_kernel.compile();

  //   double inner_duration;
  //   std::vector<double> C = m.matrix_multiply(inner_duration);
  //   bool test_ok = test_result(C);
  //   if (test_ok) {
  //     std::cout << "optimal parameters test ok!" << std::endl;
  //   } else {
  //     std::cout << "optimal parameters FAILED test!" << std::endl;
  //   }

  //   double flops = 2 * static_cast<double>(N) * static_cast<double>(N) *
  //                  static_cast<double>(N);
  //   double gflop = flops / 1E9;
  //   std::cout << "optimal inner_duration (bruteforce search): "
  //             << inner_duration << std::endl;
  //   std::cout << "[N = " << N
  //             << "] performance: " << ((repetitions * gflop) /
  //             inner_duration)
  //             << "GFLOPS" << std::endl;
  // }

  // tune with monte carlo search
  {
    std::cout << "----------------- starting tuning with monte_carlo search "
                 "------------ "
              << std::endl;
    size_t search_steps = 1000;
    autotune::tuners::monte_carlo tuner(autotune::combined_kernel,
                                        randomizable_parameters, search_steps);
    tuner.set_verbose(true);
    tuner.set_write_measurement(scenario_name + "_monte_carlo_search");

    tuner.setup_test(test_result);
    autotune::randomizable_set optimal_parameters = tuner.tune(
        m.N_org, m.A_org, m.B_org, m.repetitions, tune_kernel_duration_temp);

    std::cout << "----------------------- end tuning -----------------------"
              << std::endl;
    std::cout << "optimal parameter values (monte carlo search):" << std::endl;
    optimal_parameters.print_values();
    autotune::combined_kernel.set_parameter_values(optimal_parameters);
    autotune::combined_kernel.compile();

    double inner_duration;
    std::vector<double> C = m.matrix_multiply(inner_duration);
    bool test_ok = test_result(C);
    if (test_ok) {
      std::cout << "optimal parameters test ok!" << std::endl;
    } else {
      std::cout << "optimal parameters FAILED test!" << std::endl;
    }

    double flops = 2 * static_cast<double>(N) * static_cast<double>(N) *
                   static_cast<double>(N);
    double gflop = flops / 1E9;
    std::cout << "optimal inner_duration (monte carlo search): "
              << inner_duration << std::endl;
    std::cout << "[N = " << N
              << "] performance: " << ((repetitions * gflop) / inner_duration)
              << "GFLOPS" << std::endl;
  }
}

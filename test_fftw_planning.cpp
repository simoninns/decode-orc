#include <fftw3.h>
#include <chrono>
#include <iostream>

int main() {
    const int ZTILE = 8, YTILE = 32, XTILE = 16;
    
    double *fftReal = fftw_alloc_real(ZTILE * YTILE * XTILE);
    fftw_complex *fftComplex = fftw_alloc_complex(ZTILE * YTILE * ((XTILE/2)+1));
    
    // Test FFTW_MEASURE
    auto start = std::chrono::high_resolution_clock::now();
    fftw_plan plan_measure = fftw_plan_dft_r2c_3d(ZTILE, YTILE, XTILE, fftReal, fftComplex, FFTW_MEASURE);
    auto end = std::chrono::high_resolution_clock::now();
    auto measure_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    fftw_destroy_plan(plan_measure);
    
    // Test FFTW_ESTIMATE
    start = std::chrono::high_resolution_clock::now();
    fftw_plan plan_estimate = fftw_plan_dft_r2c_3d(ZTILE, YTILE, XTILE, fftReal, fftComplex, FFTW_ESTIMATE);
    end = std::chrono::high_resolution_clock::now();
    auto estimate_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    fftw_destroy_plan(plan_estimate);
    
    std::cout << "FFTW_MEASURE: " << measure_time << " ms" << std::endl;
    std::cout << "FFTW_ESTIMATE: " << estimate_time << " ms" << std::endl;
    std::cout << "Difference: " << (measure_time - estimate_time) << " ms" << std::endl;
    
    fftw_free(fftReal);
    fftw_free(fftComplex);
    return 0;
}

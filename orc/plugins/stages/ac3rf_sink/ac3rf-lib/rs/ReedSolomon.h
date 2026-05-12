// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef MUSECPP_REEDSOLOMON_H
#define MUSECPP_REEDSOLOMON_H

#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <spdlog/fmt/fmt.h>  // orc vendor adaptation: std::format → fmt::format (C++17 compat)
#include "GFValue.h"
#include "ByteWithErasureFlag.h"

enum DecodingStrategy {
    RS_NONE, // No correction at all
    RS_C1, // Very conservative: only corrects one error and erases the entire frame otherwise
    RS_C2, // Assumes few unflagged errors
    RS_MAX // Tries its best to correct as many erasures/errors possible (with the risk of incorrect corrections)
};

template<int irreducible_poly, int alpha_decimal> class ReedSolomon {
public:
    typedef GFValue<8, irreducible_poly, alpha_decimal> GF;

    // Notice this is only tested for k = n - 4.
    ReedSolomon(int n, int k, int fcr, DecodingStrategy decoding_strategy, bool create_diagnotics)
            : m_alpha(GF(alpha_decimal)),
              m_n(n),
              m_k(k),
              m_fcr(fcr),
              m_strategy(decoding_strategy),
              m_create_diagnotics(create_diagnotics),
              m_alpha_inverse(m_alpha.inverse()),
              m_alpha_squared(m_alpha * m_alpha),
              H() {
        for (int row = 0; row < n - k; row++) {
            std::vector<GF> h_row;
            for (int col = 0; col < n; col++) {
                h_row.push_back(GF::alpha_pow((row + fcr) * col));
            }
            H.push_back(h_row);
        }
    }

    // The first symbol in the input is the highest order coefficient
    void decode(std::vector<ByteWithErasureFlag> &data);

    void resetStatistics() {
        m_statistics.clear();
        m_diagnostics.clear();
    }

    [[nodiscard]] std::string statistics() {
        std::ostringstream ss;
        if (!m_statistics.empty() || !m_diagnostics.empty()) {
            ss << "Recent statistics: ";
            for (const auto &el: m_statistics)
                ss << el.first << ": " << el.second << ", ";
            if (m_create_diagnotics) {
                ss << "; diagnostics: ";
                for (const auto &el: m_diagnostics)
                    ss << el.first << ": " << el.second << ", ";
            }
        }
        addStatisticsToTotal();
        resetStatistics();
        ss << "Total statistics: ";
        for (const auto &el: m_total_statistics)
            ss << el.first << ": " << el.second << ", ";
        if (m_create_diagnotics) {
            ss << "; diagnostics: ";
            for (const auto &el: m_total_diagnostics)
                ss << el.first << ": " << el.second << ", ";
        }
        return ss.str();
    }

private:
    GF m_alpha;
    int m_n;
    int m_k;
    int m_fcr;
    DecodingStrategy m_strategy;
    bool m_create_diagnotics;
    GF m_alpha_inverse;
    GF m_alpha_squared;

    std::vector<std::vector<GF>> H;
    std::map<std::string, int> m_statistics;
    std::map<std::string, int> m_diagnostics;
    std::map<std::string, int> m_total_statistics;
    std::map<std::string, int> m_total_diagnostics;

    void addDiagnosticCounter(std::string const &name) {
        if (m_create_diagnotics)
            m_diagnostics[name]++;
    }

    void addStatisticsToTotal() {
        for (const auto &el: m_statistics)
            m_total_statistics[el.first] += el.second;
        for (const auto &el: m_diagnostics)
            m_total_diagnostics[el.first] += el.second;
    }

    void doDecode(std::vector<ByteWithErasureFlag> &data);

    std::vector<GF> computeSyndromes(std::vector<ByteWithErasureFlag> const &data) {
        std::vector<GF> syndromes;
        for (int i = 0; i < m_n - m_k; i++) {
            GF s = GF(0);
            for (int j = 0; j < m_n; j++)
                s = s + H[i][j] * data[j].gfValue<irreducible_poly, alpha_decimal>();
            syndromes.push_back(s);
        }
        return syndromes;
    }

    void eraseAll(std::vector<ByteWithErasureFlag> &data) {
        for (auto &b: data)
            b.setErased(true);
    }

    void uneraseAll(std::vector<ByteWithErasureFlag> &data) {
        for (auto &b: data)
            b.setErased(false);
        addDiagnosticCounter("unerased all");
    }

    bool tryCorrectOneError(std::vector<GF> const &syndromes, std::vector<ByteWithErasureFlag> &data) {
            if (std::count_if(syndromes.cbegin(), syndromes.cend(), [](GF a) -> bool { return a.isZero(); })) {
                addDiagnosticCounter("single error some syndrome zero");
                return false;
            } else {
                auto x0 = syndromes[1] * syndromes[0].inverse();
                int errorPos0 = x0.log();
                auto y0 = syndromes[0] * m_alpha.pow(errorPos0 * m_fcr).inverse();

                if (errorPos0 < 0 || errorPos0 >= m_n) {
                    addDiagnosticCounter("single error outside range");
                    return false;
                } else if (syndromes[2] * syndromes[1].inverse() != x0 ||
                           syndromes[3] * syndromes[2].inverse() != x0) {
                    addDiagnosticCounter("single error inconsistent syndromes");
                    return false;
                } else {
                    data[errorPos0] = ByteWithErasureFlag(data[errorPos0].gfValue<irreducible_poly, alpha_decimal>() + y0);
                    addDiagnosticCounter("single correction count");
                    return true;
                }
            }
    }

    bool tryCorrectTwoErrors(std::vector<GF> const &syndromes, std::vector<ByteWithErasureFlag> &data) {
            auto determinant = syndromes[1] * syndromes[1] + syndromes[0] * syndromes[2];
            if (determinant.isZero()) {
                addDiagnosticCounter("dual determinant zero");
                return false;
            } else {
                auto detInv = determinant.inverse();
                auto lambda1 = (syndromes[1] * syndromes[2] + syndromes[0] * syndromes[3]) * detInv;
                auto lambda2 = (syndromes[2] * syndromes[2] + syndromes[1] * syndromes[3]) * detInv;

                std::vector<std::pair<GF, int>> roots;
                auto alphaPowI = GF(1);
                auto lambda1TimesAlphaPowI = lambda1; // for i == 0
                auto lambda2TimesAlphaPow2I = GF(1); // we swap lambda0 (which is 1) and lambda2 in order to get the error locations directly without inversion
                for (int i = 0; i < m_n; i++) {
                    if (lambda1TimesAlphaPowI + lambda2TimesAlphaPow2I == lambda2)
                        roots.emplace_back(alphaPowI, i);
                    alphaPowI = alphaPowI * m_alpha;
                    lambda1TimesAlphaPowI = lambda1TimesAlphaPowI * m_alpha;
                    lambda2TimesAlphaPow2I = lambda2TimesAlphaPow2I * m_alpha_squared;
//        println(f"$i: ${alphaPowI.a}%02x ${alphaPowMinusI.a}%02x ${lambda1TimesAlphaPowI.a}%02x ${lambda2TimesAlphaPow2I.a}%02x")
                }

                if (roots.size() != 2) {
                    addDiagnosticCounter("dual too few roots");
                    return false;
                } else {
                    auto [x0, errorPos0] = roots[0];
                    auto [x1, errorPos1] = roots[1];
                    assert(errorPos0 >= 0 && errorPos0 < m_n && errorPos1 >= 0 && errorPos1 < m_n);

                    auto x0PlusX1Inv = (x0 + x1).inverse();
                    auto y0 = (syndromes[1] + syndromes[0] * x1) * x0PlusX1Inv * m_alpha.pow(errorPos0 * m_fcr).inverse();
                    auto y1 = (syndromes[1] + syndromes[0] * x0) * x0PlusX1Inv * m_alpha.pow(errorPos1 * m_fcr).inverse();

                    ByteWithErasureFlag data0 = data[errorPos0];
                    ByteWithErasureFlag data1 = data[errorPos1];
                    data[errorPos0] = ByteWithErasureFlag(data0.gfValue<irreducible_poly, alpha_decimal>() + y0);
                    data[errorPos1] = ByteWithErasureFlag(data1.gfValue<irreducible_poly, alpha_decimal>() + y1);

                    addDiagnosticCounter("dual correction count");
                    return true;
                }
            }
    }

    bool tryDecodeErasures(std::vector<GF> const &syndromes, std::vector<ByteWithErasureFlag> &data) {
        std::vector<int> erasure_positions;
        for (int i = 0; i < m_n; i++)
            if (data[i].isErased())
                erasure_positions.push_back(i);
        if (erasure_positions.size() > syndromes.size()) {
            addDiagnosticCounter(fmt::format("erasure decoding not performed for {} erasures", erasure_positions.size()));
            return false;
        } else {
            std::vector<GF> x(erasure_positions.size());
            std::transform(erasure_positions.begin(), erasure_positions.end(), x.begin(), [this](int b) -> GF { return m_alpha.pow(b); });
            auto s = syndromes;
            s.resize(erasure_positions.size());
            std::vector<GF> y = solveSyndromeEquations(x, s);

            for (int i = 0; i < erasure_positions.size(); i++) {
                int errorPos = erasure_positions[i];
                auto error = y[i];
                data[errorPos] = ByteWithErasureFlag(data[errorPos].gfValue<irreducible_poly, alpha_decimal>() + error * m_alpha.pow(errorPos * m_fcr).inverse());
            }

            auto new_syndromes = computeSyndromes(data);
            if (std::count_if(new_syndromes.cbegin(), new_syndromes.cend(), [](GF a) -> bool { return a.nonZero(); }) != 0) {
                // undo incorrect changes
                for (int i = 0; i < erasure_positions.size(); i++) {
                    int errorPos = erasure_positions[i];
                    auto error = y[i];
                    data[errorPos] = ByteWithErasureFlag(data[errorPos].gfValue<irreducible_poly, alpha_decimal>() + error * m_alpha.pow(errorPos * m_fcr).inverse(), true);
                }
                addDiagnosticCounter(fmt::format("erasure decoding failed for {} erasures", erasure_positions.size()));
                return false;
            } else {
                addDiagnosticCounter(fmt::format("corrected {} erasures", erasure_positions.size()));
                return true;
            }
        }
    }

    bool tryDecodeOneErrorTwoErasures(std::vector<GF> const &syndromes, std::vector<ByteWithErasureFlag> &data) {
        std::vector<int> erasure_positions;
        for (int i = 0; i < m_n; i++)
            if (data[i].isErased())
                erasure_positions.push_back(i);
        if (erasure_positions.size() != 2) {
            addDiagnosticCounter("one error two erasures needs two erasures");
            return false;
        } else {
            // See "Method for correcting both errors and erasures of RS codes using error-only and erasure-only decoding algorithms"
            // by Erl-Huei Lu, Pen-Yao Lu, Tso-Cho Chen (https://doi.org/10.1049/el.2013.1521)

            // Think of y0 and y1 being the erased errors at locators x0 and x1
            auto alphaInv = m_alpha_inverse;
            auto s0prime = syndromes[0] + syndromes[1] * alphaInv.pow(erasure_positions[0]);
            auto s1prime = syndromes[1] * alphaInv.pow(erasure_positions[0]) +
                          syndromes[2] * alphaInv.pow(2 * erasure_positions[0]);
            auto s2prime = syndromes[2] * alphaInv.pow(2 * erasure_positions[0]) +
                          syndromes[3] * alphaInv.pow(3 * erasure_positions[0]);
            int posDiff = erasure_positions[1] - erasure_positions[0]; // positive
            auto s0bis = s0prime + s1prime * alphaInv.pow(posDiff);
            auto s1bis = s1prime * alphaInv.pow(posDiff) + s2prime * alphaInv.pow(2 * posDiff);

            if (s0bis == GF(0) || s1bis == GF(0)) {
                addDiagnosticCounter("single correction with two erasures failed s0bis or s1bis zero");
                return false;
            } else {
                auto x2prime = s1bis * s0bis.inverse();
                int errorPos2prime = x2prime.log(); // this is errorPos2 - errorPos1
                int errorPos2 = (errorPos2prime + erasure_positions[1]) % 255;
                if (errorPos2 < 0 || errorPos2 >= m_n)  {
                    addDiagnosticCounter("single correction with two erasures failed errorPos2 out of range");
                    return false;
                } else {
                    auto y2prime = s0bis;
                    auto x0 = GF::alpha_pow(erasure_positions[0]);
                    auto x2 = GF::alpha_pow(errorPos2);

                    auto check1 = GF(1) + x0.inverse() * x2;
                    auto check2 = GF(1) + x2prime;
                    if (check1 == GF(0) || check2 == GF(0)) {
                        addDiagnosticCounter(fmt::format("single correction with two erasures failed zero inverse {} {}",
                                                 check1.getInt(), check2.getInt()));
                        return false;
                    } else {
                        auto y2BeforeFcrAdjust =
                                y2prime * (GF(1) + x0.inverse() * x2).inverse() * (GF(1) + x2prime).inverse();
                        auto y2 = y2BeforeFcrAdjust *
                                  GF::alpha_pow(errorPos2 * m_fcr).inverse();

                        data[errorPos2] = ByteWithErasureFlag(
                                data[errorPos2].gfValue<irreducible_poly, alpha_decimal>() + y2);

                        auto fixedSyndromes = std::vector<GF>{
                                syndromes[0] + y2BeforeFcrAdjust,
                                syndromes[1] +
                                y2BeforeFcrAdjust * GF::alpha_pow(errorPos2)
                        };

                        bool c = tryDecodeErasures(fixedSyndromes, data);
                        if (c) {
                            addDiagnosticCounter("single correction with two erasures");
                        } else {
                            // undo incorrect "correction"
                            data[errorPos2] = ByteWithErasureFlag(
                                    data[errorPos2].gfValue<irreducible_poly, alpha_decimal>() + y2);
                            addDiagnosticCounter("single correction with two erasures failed");
                        }
                        return c;
                    }
                }
            }
        }
    }

    /**
     * Solves the equation system
     *
     * x(0)^k y(0) + ... + x(n-1)^k y(n-1) = s(k), for all k = 0...n-1
     */
    std::vector<GF>
    solveSyndromeEquations(std::vector<GF> const &x, std::vector<GF> const &s) {
        int m = (int)x.size();
        assert(s.size() == m);
        assert(std::count_if(x.cbegin(), x.cend(), [](GF a) -> bool { return a.isZero(); }) == 0);

        // compute the matrix by multiplying x element wise to the previous rows
        std::vector<GF> row;
        row.resize(m);
        std::fill(row.begin(), row.end(), GF(1));

        std::vector<std::vector<GF>> mat;
        mat.resize(m);
        for (int i = 0; i < m; i++) {
            mat[i] = row;
            for (int j = 0; j < m; j++)
                row[j] *= x[j];
        }

        auto y = s;
        for (int i = 0; i < m - 1; i++) { // for each row, clear column i under it
            auto leadingInverse = mat[i][i].inverse();
            for (int j = i; j < m; j++) // make first element unit
                mat[i][j] = mat[i][j] * leadingInverse;
            y[i] *= leadingInverse;

            for (int k = i + 1; k < m; k++) { // rows under
                auto leading = mat[k][i];
                for (int j = i; j < m; j++)
                    mat[k][j] = mat[k][j] + leading * mat[i][j];
                y[k] = y[k] + leading * y[i];
            }
        }

        // make bottom right element unit
        auto brInverse = mat[m - 1][m - 1].inverse();
        mat[m - 1][m - 1] = mat[m - 1][m - 1] * brInverse;
        y[m - 1] *= brInverse;

        for (int i = m - 1; i > 0; i--) { // for each row, clear column i over it
            for (int k = i - 1; k >= 0; k--) { // rows over
                y[k] += mat[k][i] * y[i];
            }
        }

        return y;
    }
};

template<int irreducible_poly, int alpha>
void ReedSolomon<irreducible_poly, alpha>::decode(std::vector<ByteWithErasureFlag> &data) {
    assert(data.size() == m_n);

    // we want the right end of the codeword to have location 0 -- this makes the index for coefficients the same as their corresponding monomial power
    std::reverse(data.begin(), data.end());
    m_statistics["numberOfCalls"] += 1;

    doDecode(data);

    std::reverse(data.begin(), data.end());
}

template<int irreducible_poly, int alpha>
void ReedSolomon<irreducible_poly, alpha>::doDecode(std::vector<ByteWithErasureFlag> &data) {
    assert(data.size() == m_n);
    int number_of_erasures = std::count_if(data.cbegin(), data.cend(), [](ByteWithErasureFlag b) -> bool { return b.isErased(); });
    auto syndromes = computeSyndromes(data);

    auto data_copy = data; // save to print original if we think we decoded correctly but didn't

    if (std::all_of(syndromes.cbegin(), syndromes.cend(), [](GFValue<8, irreducible_poly, alpha> s) -> bool { return s.isZero(); })) {
        if (number_of_erasures == 0) {
            m_statistics["input ok"]++;
        } else {
            uneraseAll(data);
            addDiagnosticCounter(fmt::format("syndromes zero with {} erasures, un-erased", number_of_erasures));
            m_statistics["input ok despite erasures"]++;
        }
    } else {
        auto determinant = syndromes[1] * syndromes[1] + syndromes[0] * syndromes[2];

        switch (m_strategy) {
            case RS_NONE:
                break;
            case RS_C1:
                if (tryCorrectOneError(syndromes, data)) {
                    uneraseAll(data);
                    m_statistics["corrected one error"]++;
                } else if (number_of_erasures == 2) {
                    if (tryDecodeErasures(syndromes,data))
                        m_statistics["corrected erasures"]++;
                    else {
                        eraseAll(data);
                        m_statistics["failed"]++;
                    }
                } else {
                    eraseAll(data);
                    m_statistics["failed"]++;
                }
                break;

            case RS_C2:
                if (tryCorrectOneError(syndromes, data)) {
                    uneraseAll(data);
                    m_statistics["corrected one error"]++;
                } else if (number_of_erasures == 2 || number_of_erasures == 3) {
                    tryDecodeErasures(syndromes,data);
                    m_statistics["corrected erasures"]++;
                } else if (number_of_erasures == 1 && determinant != GF(0) && tryCorrectTwoErrors(syndromes, data)) {
                    int erasures_now = std::count_if(data.cbegin(), data.cend(), [](ByteWithErasureFlag b) -> bool { return b.isErased(); });
                    if (erasures_now == 0) {
                        uneraseAll(data);
                        m_statistics["corrected two errors"]++;
                    } else {
                        eraseAll(data);
                        m_statistics["failed"]++;
                    }
                } else {
                    eraseAll(data);
                    m_statistics["failed"]++;
                }
                break;

            case RS_MAX: {
                bool corrected;
                if (false && number_of_erasures >= 1) {
                    if (tryDecodeErasures(syndromes, data)) {
                        corrected = true;
                        m_statistics["corrected erasures"]++;
                    } else {
                        if (number_of_erasures == 2) // since erasure decoding failed we know we have at least one error
                            corrected = tryDecodeOneErrorTwoErasures(syndromes, data);
                        else if (number_of_erasures == 1 && determinant != GF(0))
                            corrected = tryCorrectTwoErrors(syndromes, data);
                        else if (number_of_erasures == 1 && determinant == GF(0)) // in this case the erasure is wrong -- risk of incorrect decoding?
                            corrected = tryCorrectOneError(syndromes, data);
                        else
                            corrected = false;
                        if (corrected)
                            m_statistics["corrected errors/erasures"]++;
                        else
                            m_statistics["failed"]++;
                    }
                } else {
                    if (determinant != GF(0))
                        corrected = tryCorrectTwoErrors(syndromes, data);
                    else
                        corrected = tryCorrectOneError(syndromes, data);
                    if (corrected)
                        m_statistics["corrected errors"]++;
                    else
                        m_statistics["failed"]++;
                }

                if (corrected) {
                    auto s = computeSyndromes(data);
                    int number_of_non_zero = std::count_if(s.cbegin(), s.cend(), [](GFValue<8, irreducible_poly, alpha> s) -> bool { return s.nonZero(); });
                    if (number_of_non_zero != 0) {
                        printf("Still %d non-zero syndromes after correction\n", number_of_non_zero);
                        for (int i = 0; i < data_copy.size(); i++) {
                            printf("{0x%x, %s}, ", data_copy[i].byteValue(), data_copy[i].isErased() ? "true" : "false");
                            if (i % 4 == 3)
                                printf("\n");
                        }
                        printf("\n");
                    }
                    if (number_of_erasures != 0)
                        uneraseAll(data);
                } else
                    eraseAll(data);
            }
        }
    }
}

#endif //MUSECPP_REEDSOLOMON_H

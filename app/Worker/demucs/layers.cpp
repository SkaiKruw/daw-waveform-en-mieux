#include "layers.hpp"
#include "conv.hpp"
#include "lstm.hpp"
#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>
#include <unsupported/Eigen/CXX11/Tensor>

#include <vector>
#include <cmath>
// --- Onde : normalisations réécrites pour la disposition mémoire « column-major » d'Eigen ---------------
// Mêmes formules que demucs.cpp (variance non biaisée, n - 1), mais parcours contigu en mémoire
// (l'original sautait de F x C flottants à chaque accès) et accumulation en double précision.
namespace
{
    // Moyenne et inverse de l'écart-type, pour chaque ligne i, de x[i, c, w] avec c dans [c0, c1[ et w dans [0, W[.
    void rowStats (const float *xd, int F, int C, int W, int c0, int c1,
                   std::vector<double> &sum, std::vector<float> &mean, std::vector<float> &inv, float eps)
    {
        std::fill(sum.begin(), sum.end(), 0.0);
        const double n = (double)(c1 - c0) * W;
        for (int w = 0; w < W; ++w)
            for (int c = c0; c < c1; ++c)
            {
                const float *p = xd + (size_t)F * ((size_t)c + (size_t)C * w);
                for (int i = 0; i < F; ++i)
                    sum[i] += p[i];
            }
        for (int i = 0; i < F; ++i)
            mean[i] = (float)(sum[i] / n);
        std::fill(sum.begin(), sum.end(), 0.0);
        for (int w = 0; w < W; ++w)
            for (int c = c0; c < c1; ++c)
            {
                const float *p = xd + (size_t)F * ((size_t)c + (size_t)C * w);
                for (int i = 0; i < F; ++i)
                {
                    const double d = (double)p[i] - mean[i];
                    sum[i] += d * d;
                }
            }
        for (int i = 0; i < F; ++i)
            inv[i] = 1.0f / std::sqrt((float)(sum[i] / (n - 1.0)) + eps);
    }
}

Eigen::Tensor3dXf demucscpp::group_norm(const Eigen::Tensor3dXf &x,
                                        const Eigen::Tensor1dXf &weight,
                                        const Eigen::Tensor1dXf &b,
                                        int num_groups, float eps)
{
    const int F = (int)x.dimension(0), C = (int)x.dimension(1), W = (int)x.dimension(2);
    const int gs = C / num_groups;
    Eigen::Tensor3dXf y_out(F, C, W);
    const float *xd = x.data();
    float *yd = y_out.data();
    std::vector<double> sum((size_t)F);
    std::vector<float> mean((size_t)F), inv((size_t)F);
    for (int g = 0; g < num_groups; ++g)
    {
        rowStats(xd, F, C, W, g * gs, (g + 1) * gs, sum, mean, inv, eps);
        for (int w = 0; w < W; ++w)
            for (int c = g * gs; c < (g + 1) * gs; ++c)
            {
                const size_t o = (size_t)F * ((size_t)c + (size_t)C * w);
                const float wc = weight(c), bc = b(c);
                for (int i = 0; i < F; ++i)
                    yd[o + i] = (xd[o + i] - mean[i]) * inv[i] * wc + bc;
            }
    }
    return y_out;
}

Eigen::Tensor3dXf
demucscpp::group_norm_fused_gelu(const Eigen::Tensor3dXf &x,
                                 const Eigen::Tensor1dXf &weight,
                                 const Eigen::Tensor1dXf &bias, float eps)
{
    const int F = (int)x.dimension(0), C = (int)x.dimension(1), W = (int)x.dimension(2);
    Eigen::Tensor3dXf y_out(F, C, W);
    const float *xd = x.data();
    float *yd = y_out.data();
    std::vector<double> sum((size_t)F);
    std::vector<float> mean((size_t)F), inv((size_t)F);
    rowStats(xd, F, C, W, 0, C, sum, mean, inv, eps);
    const float rs2 = 1.0f / std::sqrt(2.0f);
    for (int w = 0; w < W; ++w)
        for (int c = 0; c < C; ++c)
        {
            const size_t o = (size_t)F * ((size_t)c + (size_t)C * w);
            const float wc = weight(c), bc = bias(c);
            for (int i = 0; i < F; ++i)
            {
                const float v = (xd[o + i] - mean[i]) * inv[i] * wc + bc;
                yd[o + i] = 0.5f * v * (1.0f + std::erf(v * rs2));
            }
        }
    return y_out;
}

Eigen::Tensor3dXf demucscpp::glu(const Eigen::Tensor3dXf &x, const int dim)
{
    if (x.dimension(dim) % 2 != 0)
    {
        std::cerr << "Dimension size must be evenly divisible by 2"
                  << std::endl;
        std::exit(1);
    }

    int split_size = x.dimension(dim) / 2;

    Eigen::array<int, 3> start_indices = {0, 0, 0};
    Eigen::array<int, 3> sizes = {(int)x.dimension(0), (int)x.dimension(1),
                                  (int)x.dimension(2)};
    start_indices[dim] = split_size;
    sizes[dim] = split_size;

    auto first_half = x.slice(Eigen::array<int, 3>({0, 0, 0}), sizes);
    auto second_half = x.slice(start_indices, sizes);
    auto sigmoid_second_half = second_half.unaryExpr(
        [](float v) { return 1.0f / (1.0f + std::exp(-v)); });

    return first_half * sigmoid_second_half;
}

Eigen::Tensor3dXf demucscpp::layer_norm(const Eigen::Tensor3dXf &x,
                                        const Eigen::Tensor1dXf &weight,
                                        const Eigen::Tensor1dXf &bias,
                                        float eps)
{
    // Normalisation sur le dernier axe (w) pour chaque (i, c) : statistiques par ligne i, colonne c fixée.
    const int F = (int)x.dimension(0), C = (int)x.dimension(1), W = (int)x.dimension(2);
    Eigen::Tensor3dXf y_out(F, C, W);
    const float *xd = x.data();
    float *yd = y_out.data();
    std::vector<double> sum((size_t)F);
    std::vector<float> mean((size_t)F), inv((size_t)F);
    for (int c = 0; c < C; ++c)
    {
        // rowStats sur une seule « colonne » c : on lui passe c0 = c, c1 = c + 1.
        rowStats(xd, F, C, W, c, c + 1, sum, mean, inv, eps);
        for (int w = 0; w < W; ++w)
        {
            const size_t o = (size_t)F * ((size_t)c + (size_t)C * w);
            const float ww = weight(w), bw = bias(w);
            for (int i = 0; i < F; ++i)
                yd[o + i] = (xd[o + i] - mean[i]) * inv[i] * ww + bw;
        }
    }
    return y_out;
}

void demucscpp::apply_dconv(const struct demucscpp::demucs_model &model,
                            Eigen::Tensor3dXf &y, int freq_idx, int encdec_idx,
                            int layer_idx, int mid_crop)
{
    // store another copy of y to sum back later
    Eigen::Tensor3dXf y_copy = y;

    // now dconv time

    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<48, 6, 3, 1, 1, 1>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 1:
        y = demucscpp::conv1d<96, 12, 3, 1, 1, 1>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 2:
        y = demucscpp::conv1d<192, 24, 3, 1, 1, 1>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 3:
        y = demucscpp::conv1d<384, 48, 3, 1, 1, 1>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    };

    y = demucscpp::group_norm_fused_gelu(
        y,
        model.dconv_layers_1_groupnorm_weight[freq_idx][encdec_idx][layer_idx]
                                             [0],
        model.dconv_layers_1_groupnorm_bias[freq_idx][encdec_idx][layer_idx][0],
        1e-05);

    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<6, 96, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 1:
        y = demucscpp::conv1d<12, 192, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 2:
        y = demucscpp::conv1d<24, 384, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    case 3:
        y = demucscpp::conv1d<48, 768, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [0],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [0]);
        break;
    };

    y = demucscpp::group_norm(
        y,
        model.dconv_layers_4_groupnorm_weight[freq_idx][encdec_idx][layer_idx]
                                             [0],
        model.dconv_layers_4_groupnorm_bias[freq_idx][encdec_idx][layer_idx][0],
        1, 1e-05);

    y = demucscpp::glu(y, 1);

    y = demucscpp::layer_scale(
        y, model.dconv_layers_6_scale[freq_idx][encdec_idx][layer_idx][0]);

    // now we add y to itself
    y = y + y_copy;

    // store another copy of y to sum back later
    y_copy = y;

    // NEXT ENTIRE SUBSEQUENCE OF DCONV WITH SLIGHTLY DIFFERENT PARAMS

    // Conv1d(48, 6, kernel_size=(3,), stride=(1,), padding=(2,), dilation=(2,))
    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<48, 6, 3, 1, 2, 2>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 1:
        y = demucscpp::conv1d<96, 12, 3, 1, 2, 2>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 2:
        y = demucscpp::conv1d<192, 24, 3, 1, 2, 2>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 3:
        y = demucscpp::conv1d<384, 48, 3, 1, 2, 2>(
            y,
            model.dconv_layers_0_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_0_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    };

    Eigen::Tensor3dXf y_cropped =
        y.slice(Eigen::array<Eigen::Index, 3>({0, 0, 0}),
                Eigen::array<Eigen::Index, 3>(
                    {y.dimension(0), y.dimension(1), mid_crop}));

    y = y_cropped;

    y = demucscpp::group_norm_fused_gelu(
        y,
        model.dconv_layers_1_groupnorm_weight[freq_idx][encdec_idx][layer_idx]
                                             [1],
        model.dconv_layers_1_groupnorm_bias[freq_idx][encdec_idx][layer_idx][1],
        1e-05);

    // Conv1d(6, 96, kernel_size=(1,), stride=(1,))
    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<6, 96, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 1:
        y = demucscpp::conv1d<12, 192, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 2:
        y = demucscpp::conv1d<24, 384, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    case 3:
        y = demucscpp::conv1d<48, 768, 1, 1, 0, 1>(
            y,
            model.dconv_layers_3_conv1d_weight[freq_idx][encdec_idx][layer_idx]
                                              [1],
            model.dconv_layers_3_conv1d_bias[freq_idx][encdec_idx][layer_idx]
                                            [1]);
        break;
    };

    y = demucscpp::group_norm(
        y,
        model.dconv_layers_4_groupnorm_weight[freq_idx][encdec_idx][layer_idx]
                                             [1],
        model.dconv_layers_4_groupnorm_bias[freq_idx][encdec_idx][layer_idx][1],
        1, 1e-05);

    y = demucscpp::glu(y, 1);
    y = demucscpp::layer_scale(
        y, model.dconv_layers_6_scale[freq_idx][encdec_idx][layer_idx][1]);

    // if y_copy is shorter than y in the last dim
    // pad the last dim with zeros to match

    if (y_copy.dimension(2) < y.dimension(2))
    {
        // pad the last dim with zeros to match
        Eigen::Tensor3dXf padded_tensor_copy(
            y_copy.dimension(0), y_copy.dimension(1), y.dimension(2));
        padded_tensor_copy.setZero();
        padded_tensor_copy.slice(Eigen::array<Eigen::Index, 3>({0, 0, 0}),
                                 y_copy.dimensions()) = y_copy;
        y_copy = padded_tensor_copy;
    }

    // now sum with itself
    y = y + y_copy;
}

void demucscpp::common_encoder_layer(
    Eigen::Tensor3dXf &q,       // q = x = frequency
    const Eigen::Tensor3dXf &k, // k = xt = time
    const Eigen::Tensor1dXf &norm1_weight, const Eigen::Tensor1dXf &norm1_bias,
    const Eigen::Tensor1dXf &norm2_weight, const Eigen::Tensor1dXf &norm2_bias,
    const Eigen::MatrixXf &in_proj_weight, const Eigen::VectorXf &in_proj_bias,
    const Eigen::MatrixXf &out_proj_weight,
    const Eigen::VectorXf &out_proj_bias, const Eigen::VectorXf &gamma1_scale,
    const Eigen::Tensor1dXf &norm3_weight, const Eigen::Tensor1dXf &norm3_bias,
    const Eigen::MatrixXf &linear1_weight, const Eigen::VectorXf &linear1_bias,
    const Eigen::MatrixXf &linear2_weight, const Eigen::VectorXf &linear2_bias,
    const Eigen::VectorXf &gamma2_scale,
    const Eigen::Tensor1dXf &norm_out_weight,
    const Eigen::Tensor1dXf &norm_out_bias, const int num_heads,
    // optional params
    float eps, const bool self_attention)
{
    // Normalize x using the norm1 weights and biases
    Eigen::Tensor3dXf q_norm =
        demucscpp::layer_norm(q, norm1_weight, norm1_bias, eps);

    Eigen::Tensor3dXf k_norm;
    if (self_attention)
    {
        k_norm = q_norm;
    }
    else
    {
        k_norm = demucscpp::layer_norm(k, norm2_weight, norm2_bias, eps);
    }

    // Cross-attention block
    // Compute Q, K, V matrices

    int B = q.dimension(0);
    int T = q.dimension(1);
    int C = q.dimension(2);

    int S = k.dimension(1);

    // Reshape q, k to 2D matrix of dimensions (T*B, C)

    // Use Eigen::Map to avoid manual loops for reshaping
    Eigen::MatrixXf q_norm_2d =
        Eigen::Map<const Eigen::MatrixXf>(q_norm.data(), T, C);
    Eigen::MatrixXf k_norm_2d =
        Eigen::Map<const Eigen::MatrixXf>(k_norm.data(), S, C);

    // Compute Q, K, V matrices
    Eigen::MatrixXf Q =
        q_norm_2d * in_proj_weight.block(0, 0, C, C).transpose();
    Eigen::MatrixXf K =
        k_norm_2d * in_proj_weight.block(C, 0, C, C).transpose();
    Eigen::MatrixXf V =
        k_norm_2d * in_proj_weight.block(2 * C, 0, C, C).transpose();

    Eigen::VectorXf q_bias = in_proj_bias.segment(0, C);
    Eigen::VectorXf k_bias = in_proj_bias.segment(C, C);
    Eigen::VectorXf v_bias = in_proj_bias.segment(2 * C, C);

    // copied from linear layer: ff1.rowwise() += linear1_bias.transpose();
    Q.rowwise() += q_bias.transpose();
    K.rowwise() += k_bias.transpose();
    V.rowwise() += v_bias.transpose();

    int head_split = C / num_heads;

    // map matrices to tensors
    Eigen::Tensor3dXf Q_heads =
        Eigen::TensorMap<Eigen::Tensor3dXf>(Q.data(), T, head_split, num_heads);
    Eigen::Tensor3dXf K_heads =
        Eigen::TensorMap<Eigen::Tensor3dXf>(K.data(), S, head_split, num_heads);
    Eigen::Tensor3dXf V_heads =
        Eigen::TensorMap<Eigen::Tensor3dXf>(V.data(), S, head_split, num_heads);

    Eigen::MatrixXf cross_attn_out(T, C);

    for (int h = 0; h < num_heads; ++h)
    {
        // Extract the h-th head from Q_heads and K_heads
        Eigen::Tensor2dXf Q_head_tensor = Q_heads.chip(h, 2);
        Eigen::Tensor2dXf K_head_tensor = K_heads.chip(h, 2);
        Eigen::Tensor2dXf V_head_tensor = V_heads.chip(h, 2);

        // Reshape the tensors to matrices
        Eigen::Map<Eigen::MatrixXf> Q_head(Q_head_tensor.data(), T, head_split);
        Eigen::Map<Eigen::MatrixXf> K_head(K_head_tensor.data(), S, head_split);
        Eigen::Map<Eigen::MatrixXf> V_head(V_head_tensor.data(), S, head_split);

        // Onde : scores calculés transposés (S x T) pour que le softmax de chaque requête porte sur une
        // colonne contiguë en mémoire, sans matrices temporaires répliquées. Résultat identique.
        Eigen::MatrixXf scores_t;
        scores_t.noalias() = K_head * Q_head.transpose();
        scores_t *= 1.0f / std::sqrt((float)head_split);
        for (int t = 0; t < T; ++t)
        {
            auto col = scores_t.col(t);
            const float m = col.maxCoeff();
            col.array() = (col.array() - m).exp();
            col *= 1.0f / col.sum();
        }

        Eigen::MatrixXf cross_attn_head;
        cross_attn_head.noalias() = scores_t.transpose() * V_head;
        cross_attn_out.block(0, h * head_split, T, head_split) =
            cross_attn_head;
    }

    // Copy q into q_2d (Map q to 2D matrix)
    Eigen::Map<Eigen::MatrixXf> q_2d(q.data(), T, C);

    // Apply output projection with gamma1_scale
    Eigen::MatrixXf out_proj = cross_attn_out * out_proj_weight.transpose();
    out_proj.array().rowwise() += out_proj_bias.transpose().array();
    out_proj = out_proj.array().rowwise() * gamma1_scale.transpose().array();

    // Add to q
    q_2d += out_proj;

    // before feedforward, apply norm3 to x i.e. q
    q_norm = demucscpp::layer_norm(q, norm3_weight, norm3_bias, eps);
    q_norm_2d = Eigen::Map<const Eigen::MatrixXf>(q_norm.data(), T, C);

    // Feedforward block
    // Linear layer 1
    Eigen::MatrixXf ff1 = q_norm_2d * linear1_weight.transpose();
    ff1.rowwise() += linear1_bias.transpose();

    ff1 = demucscpp::gelu(ff1);

    // Linear layer 2
    Eigen::MatrixXf ff2 = ff1 * linear2_weight.transpose();
    ff2.rowwise() += linear2_bias.transpose();

    // Apply gamma_2 scale directly on 2D matrix
    ff2 = ff2.array().rowwise() * gamma2_scale.transpose().array();

    // now x = x + self.gamma_2(self._ff_block(self.norm3(q))))
    q_2d += ff2;

    // Map the 2D data back into a 3D tensor with dimensions (T, B, C)
    q = Eigen::TensorMap<Eigen::Tensor3dXf>(q_2d.data(), T, B, C);

    // Swap the first and last dimensions to get a tensor with dimensions (B, C,
    // T)
    Eigen::array<int, 3> permute_dims_3 = {1, 2, 0};
    Eigen::Tensor3dXf q_shuf = q.shuffle(permute_dims_3);

    // Normalize the output with norm_out/MyGroupNorm
    q = demucscpp::group_norm(q_shuf, norm_out_weight, norm_out_bias, 1, eps);

    Eigen::array<int, 3> permute_dims_4 = {0, 2, 1};
    q_shuf = q.shuffle(permute_dims_4);

    q = q_shuf;
}

void demucscpp_v3::local_attention(
    Eigen::Tensor3dXf &x, // x = frequency, time, or combined
                          // input tensor [B, C, T]
    const Eigen::Tensor3dXf &content_weight,
    const Eigen::Tensor1dXf &content_bias,
    const Eigen::Tensor3dXf &query_weight, const Eigen::Tensor1dXf &query_bias,
    const Eigen::Tensor3dXf &key_weight, const Eigen::Tensor1dXf &key_bias,
    const Eigen::Tensor3dXf &query_decay_weight,
    const Eigen::Tensor1dXf &query_decay_bias,
    const Eigen::Tensor2dXf &query_decay_kernel,
    const Eigen::Tensor3dXf &proj_weight, const Eigen::Tensor1dXf &proj_bias,
    const int hidden_size)
{
    // local-attention block

    int B = x.dimension(0);
    int C = x.dimension(1);
    int T = x.dimension(2);

    const int num_heads = demucscpp_v3::LOCAL_ATTN_N_HEADS;

    // apply query conv1d on x
    Eigen::Tensor3dXf queries;
    Eigen::Tensor3dXf query_decays;
    Eigen::Tensor3dXf keys;
    Eigen::Tensor3dXf content;

    if (hidden_size == 192)
    {
        queries = demucscpp::conv1d<192, 192, 1, 1, 0, 1>(x, query_weight,
                                                          query_bias);
        keys = demucscpp::conv1d<192, 192, 1, 1, 0, 1>(x, key_weight, key_bias);
        query_decays = demucscpp::conv1d<192, 16, 1, 1, 0, 1>(
            x, query_decay_weight, query_decay_bias);
        content = demucscpp::conv1d<192, 192, 1, 1, 0, 1>(x, content_weight,
                                                          content_bias);
    }
    else
    {
        queries = demucscpp::conv1d<384, 384, 1, 1, 0, 1>(x, query_weight,
                                                          query_bias);
        keys = demucscpp::conv1d<384, 384, 1, 1, 0, 1>(x, key_weight, key_bias);
        query_decays = demucscpp::conv1d<384, 16, 1, 1, 0, 1>(
            x, query_decay_weight, query_decay_bias);
        content = demucscpp::conv1d<384, 384, 1, 1, 0, 1>(x, content_weight,
                                                          content_bias);
    }

    // so far, this is correct and matches pytorch

    int features_per_head = C / num_heads;

    // now implement dots calculation

    // Initialize the dots tensor
    Eigen::Tensor4dXf dots(B, num_heads, T, T);
    dots.setZero();

    // Precompute the square root of features_per_head
    float sqrt_features_per_head = std::sqrt(features_per_head);

    // apply a sigmoid activation with a 1/2 incorporated
    query_decays = query_decays.unaryExpr(
        [](float v) { return 0.5f / (1.0f + std::exp(-v)); });

    // Initialize the weights tensor for softmax
    Eigen::Tensor4dXf weights(B, num_heads, T, T);

    // Loop structure to compute both dot products and apply decay
    // simultaneously
    for (int b = 0; b < B; ++b)
    {
        for (int h = 0; h < num_heads; ++h)
        {
            for (int t = 0; t < T; ++t)
            {
                for (int s = 0; s < T; ++s)
                {
                    float dot_product = 0.0f;
                    float decay_effect = 0.0f;

                    // Compute the standard dot product
                    for (int c = 0; c < features_per_head; ++c)
                    {
                        int channel_index = h * features_per_head + c;
                        dot_product += queries(b, channel_index, s) *
                                       keys(b, channel_index, t);
                    }
                    dots(b, h, t, s) = dot_product / sqrt_features_per_head;

                    // Calculate decay effect for this dot product
                    for (int n = 0; n < LOCAL_ATTN_N_DECAY; ++n)
                    {
                        int decay_index = std::abs(
                            t - s); // Assuming decay_kernel is indexed by delta
                        float decay_kernel_value =
                            query_decay_kernel(n, decay_index);

                        // Transform query_decay by applying sigmoid directly
                        // here
                        float decay_query_value =
                            query_decays(b, h * LOCAL_ATTN_N_DECAY + n, s);

                        decay_effect += decay_kernel_value * decay_query_value;
                    }

                    // Apply decay effect directly to the dot product
                    if (t != s)
                    {
                        dots(b, h, t, s) += decay_effect;
                    }
                    else
                    {
                        dots(b, h, t, s) = -100.0f;
                    }
                }
            }

            for (int t = 0; t < T; ++t)
            {
                float max_val = -std::numeric_limits<float>::infinity();
                for (int s = 0; s < T; ++s)
                {
                    if (dots(b, h, s, t) > max_val)
                    {
                        max_val = dots(b, h, s, t);
                    }
                }

                float sum_exp = 0.0f;
                // Calculate the exponentials and sum them
                for (int s = 0; s < T; ++s)
                {
                    weights(b, h, s, t) = std::exp(dots(b, h, s, t) - max_val);
                    sum_exp += weights(b, h, s, t);
                }

                // Normalize the weights to form a proper probability
                // distribution
                for (int s = 0; s < T; ++s)
                {
                    weights(b, h, s, t) /= sum_exp;
                }
            }
        }
    }

    // Initialize the reshaped result tensor directly
    Eigen::Tensor3dXf reshaped_result(B, C, T);
    reshaped_result.setZero();

    // Merge computation of result tensor and reshaping
    for (int b = 0; b < B; ++b)
    {
        for (int h = 0; h < num_heads; ++h)
        {
            for (int c = 0; c < C / num_heads; ++c)
            {
                for (int s = 0; s < T; ++s)
                {
                    for (int t = 0; t < T; ++t)
                    {
                        // Directly update the reshaped_result tensor
                        int full_channel_index = h * (C / num_heads) + c;
                        reshaped_result(b, full_channel_index, s) +=
                            weights(b, h, t, s) *
                            content(b, h * (C / num_heads) + c, t);
                    }
                }
            }
        }
    }

    // Apply projection layer
    Eigen::Tensor3dXf projected_result;
    if (hidden_size == 192)
    {
        projected_result = demucscpp::conv1d<192, 192, 1, 1, 0, 1>(
            reshaped_result, proj_weight, proj_bias);
    }
    else
    {
        projected_result = demucscpp::conv1d<384, 384, 1, 1, 0, 1>(
            reshaped_result, proj_weight, proj_bias);
    }

    // Add x to projected_result
    x += projected_result;
}

void demucscpp_v3::apply_dconv_v3(
    const struct demucscpp_v3::demucs_v3_model &model, Eigen::Tensor3dXf &y,
    int freq_idx, int layer_idx, int mid_crop)
{
    // store another copy of y to sum back later
    Eigen::Tensor3dXf y_copy = y;

    // now dconv time

    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<48, 12, 3, 1, 1, 1>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 1:
        y = demucscpp::conv1d<96, 24, 3, 1, 1, 1>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 2:
        y = demucscpp::conv1d<192, 48, 3, 1, 1, 1>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 3:
        y = demucscpp::conv1d<384, 96, 3, 1, 1, 1>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    };

    // y = demucscpp_v3::groupnorm::group_norm_fused_gelu(
    //     y,
    //     model.dconv_layers_1_groupnorm_weight[freq_idx][layer_idx]
    //                                          [0],
    //     model.dconv_layers_1_groupnorm_bias[freq_idx][layer_idx][0],
    //     1,
    //     1e-05);

    y = demucscpp::group_norm_fused_gelu(
        y, model.dconv_layers_1_groupnorm_weight[freq_idx][layer_idx][0],
        model.dconv_layers_1_groupnorm_bias[freq_idx][layer_idx][0], 1e-05);

    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<12, 96, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 1:
        y = demucscpp::conv1d<24, 192, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 2:
        y = demucscpp::conv1d<48, 384, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    case 3:
        y = demucscpp::conv1d<96, 768, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][0],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][0]);
        break;
    };

    y = demucscpp::group_norm(
        y, model.dconv_layers_4_groupnorm_weight[freq_idx][layer_idx][0],
        model.dconv_layers_4_groupnorm_bias[freq_idx][layer_idx][0], 1, 1e-05);

    y = demucscpp::glu(y, 1);

    y = demucscpp::layer_scale(
        y, model.dconv_layers_6_scale[freq_idx][layer_idx][0]);

    // now we add y to itself
    y = y + y_copy;

    // store another copy of y to sum back later
    y_copy = y;

    // NEXT ENTIRE SUBSEQUENCE OF DCONV WITH SLIGHTLY DIFFERENT PARAMS

    // Conv1d(48, 6, kernel_size=(3,), stride=(1,), padding=(2,), dilation=(2,))
    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<48, 12, 3, 1, 2, 2>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 1:
        y = demucscpp::conv1d<96, 24, 3, 1, 2, 2>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 2:
        y = demucscpp::conv1d<192, 48, 3, 1, 2, 2>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 3:
        y = demucscpp::conv1d<384, 96, 3, 1, 2, 2>(
            y, model.dconv_layers_0_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_0_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    };

    Eigen::Tensor3dXf y_cropped =
        y.slice(Eigen::array<Eigen::Index, 3>({0, 0, 0}),
                Eigen::array<Eigen::Index, 3>(
                    {y.dimension(0), y.dimension(1), mid_crop}));

    y = y_cropped;

    y = demucscpp::group_norm_fused_gelu(
        y, model.dconv_layers_1_groupnorm_weight[freq_idx][layer_idx][1],
        model.dconv_layers_1_groupnorm_bias[freq_idx][layer_idx][1], 1e-05);

    // Conv1d(6, 96, kernel_size=(1,), stride=(1,))
    switch (layer_idx)
    {
    case 0:
        y = demucscpp::conv1d<12, 96, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 1:
        y = demucscpp::conv1d<24, 192, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 2:
        y = demucscpp::conv1d<48, 384, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    case 3:
        y = demucscpp::conv1d<96, 768, 1, 1, 0, 1>(
            y, model.dconv_layers_3_conv1d_weight[freq_idx][layer_idx][1],
            model.dconv_layers_3_conv1d_bias[freq_idx][layer_idx][1]);
        break;
    };

    y = demucscpp::group_norm(
        y, model.dconv_layers_4_groupnorm_weight[freq_idx][layer_idx][1],
        model.dconv_layers_4_groupnorm_bias[freq_idx][layer_idx][1], 1, 1e-05);

    y = demucscpp::glu(y, 1);
    y = demucscpp::layer_scale(
        y, model.dconv_layers_6_scale[freq_idx][layer_idx][1]);

    // if y_copy is shorter than y in the last dim
    // pad the last dim with zeros to match

    if (y_copy.dimension(2) < y.dimension(2))
    {
        // pad the last dim with zeros to match
        Eigen::Tensor3dXf padded_tensor_copy(
            y_copy.dimension(0), y_copy.dimension(1), y.dimension(2));
        padded_tensor_copy.setZero();
        padded_tensor_copy.slice(Eigen::array<Eigen::Index, 3>({0, 0, 0}),
                                 y_copy.dimensions()) = y_copy;
        y_copy = padded_tensor_copy;
    }

    // now sum with itself
    y = y + y_copy;
}

void demucscpp_v3::apply_dconv_v3_encoder_4_5(
    const struct demucscpp_v3::demucs_v3_model &model, Eigen::Tensor3dXf &y,
    int encoder_idx, int mid_crop,
    struct demucscpp_v3::demucs_v3_segment_buffers &buffers)
{
    int lstm_hidden_size = encoder_idx == 0 ? demucscpp_v3::LSTM_HIDDEN_SIZE_0
                                            : demucscpp_v3::LSTM_HIDDEN_SIZE_1;

    // store another copy of y to sum back later
    Eigen::Tensor3dXf y_copy = y;

    // now dconv time

    switch (encoder_idx)
    {
    case 0:
        y = demucscpp::conv1d<768, 192, 3, 1, 1, 1>(
            y, model.encoder_4_5_dconv_layers_0_conv1d_weight[encoder_idx][0],
            model.encoder_4_5_dconv_layers_0_conv1d_bias[encoder_idx][0]);
        break;
    case 1:
        y = demucscpp::conv1d<1536, 384, 3, 1, 1, 1>(
            y, model.encoder_4_5_dconv_layers_0_conv1d_weight[encoder_idx][0],
            model.encoder_4_5_dconv_layers_0_conv1d_bias[encoder_idx][0]);
        break;
    };

    y = demucscpp::group_norm_fused_gelu(
        y, model.encoder_4_5_dconv_layers_1_groupnorm_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_1_groupnorm_bias[encoder_idx][0], 1e-05);

    // transpose it to put time seq last
    Eigen::MatrixXf y_mat =
        Eigen::Map<Eigen::MatrixXf>(y.data(), y.dimension(1), y.dimension(2))
            .transpose();

    // then, bilstm
    demucscpp_v3::lstm_forward(model, encoder_idx, 0, y_mat, buffers,
                               lstm_hidden_size);

    // access last element of the last dim which is the output of the bilstm
    Eigen::MatrixXf lstm_out_0 = buffers.lstm_output[encoder_idx][0][1];

    // set lstm state to 0
    demucscpp_v3::lstm_reset_zero(encoder_idx, 0, buffers);

    // apply the linear layer on the lstm_out_0
    lstm_out_0 = (lstm_out_0 *
                  model.encoder_4_5_dconv_layers_3_linear_weight[encoder_idx][0]
                      .transpose())
                     .rowwise() +
                 model.encoder_4_5_dconv_layers_3_linear_bias[encoder_idx][0]
                     .transpose();

    // then apply skip connection
    lstm_out_0 = lstm_out_0 + y_mat;

    // copy it to a original 3d tensor
    y = Eigen::TensorMap<Eigen::Tensor3dXf>(
        lstm_out_0.data(), lstm_out_0.rows(), 1, lstm_out_0.cols());

    // swap dims from 0,1,2 to 1,2,0
    Eigen::Tensor3dXf y_shuff = y.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // then, localattn
    demucscpp_v3::local_attention(
        y_shuff,
        model.encoder_4_5_dconv_layers_4_content_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_content_bias[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_query_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_query_bias[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_key_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_key_bias[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_query_decay_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_query_decay_bias[encoder_idx][0],
        buffers.local_attn_decay_kernel,
        model.encoder_4_5_dconv_layers_4_proj_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_4_proj_bias[encoder_idx][0],
        lstm_hidden_size);

    y = y_shuff;

    switch (encoder_idx)
    {
    case 0:
        y = demucscpp::conv1d<192, 1536, 1, 1, 0, 1>(
            y, model.encoder_4_5_dconv_layers_5_conv1d_weight[encoder_idx][0],
            model.encoder_4_5_dconv_layers_5_conv1d_bias[encoder_idx][0]);
        break;
    case 1:
        y = demucscpp::conv1d<384, 3072, 1, 1, 0, 1>(
            y, model.encoder_4_5_dconv_layers_5_conv1d_weight[encoder_idx][0],
            model.encoder_4_5_dconv_layers_5_conv1d_bias[encoder_idx][0]);
        break;
    };

    y = demucscpp::group_norm(
        y, model.encoder_4_5_dconv_layers_6_groupnorm_weight[encoder_idx][0],
        model.encoder_4_5_dconv_layers_6_groupnorm_bias[encoder_idx][0], 1,
        1e-05);

    y = demucscpp::glu(y, 1);

    y = demucscpp::layer_scale(
        y, model.encoder_4_5_dconv_layers_8_scale[encoder_idx][0]);

    // now we add y to itself
    y = y + y_copy;

    // store another copy of y to sum back later
    y_copy = y;

    // NEXT ENTIRE SUBSEQUENCE OF DCONV WITH SLIGHTLY DIFFERENT PARAMS
    // now dconv time

    switch (encoder_idx)
    {
    case 0:
        y = demucscpp::conv1d<768, 192, 3, 1, 2, 2>(
            y, model.encoder_4_5_dconv_layers_0_conv1d_weight[encoder_idx][1],
            model.encoder_4_5_dconv_layers_0_conv1d_bias[encoder_idx][1]);
        break;
    case 1:
        y = demucscpp::conv1d<1536, 384, 3, 1, 2, 2>(
            y, model.encoder_4_5_dconv_layers_0_conv1d_weight[encoder_idx][1],
            model.encoder_4_5_dconv_layers_0_conv1d_bias[encoder_idx][1]);
        break;
    };

    Eigen::Tensor3dXf y_cropped =
        y.slice(Eigen::array<Eigen::Index, 3>({0, 0, 0}),
                Eigen::array<Eigen::Index, 3>(
                    {y.dimension(0), y.dimension(1), mid_crop}));

    y = y_cropped;

    y = demucscpp::group_norm_fused_gelu(
        y, model.encoder_4_5_dconv_layers_1_groupnorm_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_1_groupnorm_bias[encoder_idx][1], 1e-05);

    // transpose it to put time seq last
    y_mat =
        Eigen::Map<Eigen::MatrixXf>(y.data(), y.dimension(1), y.dimension(2))
            .transpose();

    // then, bilstm
    demucscpp_v3::lstm_forward(model, encoder_idx, 1, y_mat, buffers,
                               lstm_hidden_size);

    // access last element of the last dim which is the output of the bilstm
    lstm_out_0 = buffers.lstm_output[encoder_idx][1][1];

    // reset lstm state to 0
    demucscpp_v3::lstm_reset_zero(encoder_idx, 1, buffers);

    // apply the linear layer on the lstm_out_0
    lstm_out_0 = (lstm_out_0 *
                  model.encoder_4_5_dconv_layers_3_linear_weight[encoder_idx][1]
                      .transpose())
                     .rowwise() +
                 model.encoder_4_5_dconv_layers_3_linear_bias[encoder_idx][1]
                     .transpose();

    // then apply skip connection
    lstm_out_0 = lstm_out_0 + y_mat;

    // copy it to a original 3d tensor
    y = Eigen::TensorMap<Eigen::Tensor3dXf>(
        lstm_out_0.data(), lstm_out_0.rows(), 1, lstm_out_0.cols());

    // swap dims from 0,1,2 to 1,2,0
    y_shuff = y.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // then, localattn
    demucscpp_v3::local_attention(
        y_shuff,
        model.encoder_4_5_dconv_layers_4_content_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_content_bias[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_query_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_query_bias[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_key_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_key_bias[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_query_decay_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_query_decay_bias[encoder_idx][1],
        buffers.local_attn_decay_kernel,
        model.encoder_4_5_dconv_layers_4_proj_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_4_proj_bias[encoder_idx][1],
        lstm_hidden_size);

    y = y_shuff;

    switch (encoder_idx)
    {
    case 0:
        y = demucscpp::conv1d<192, 1536, 1, 1, 0, 1>(
            y, model.encoder_4_5_dconv_layers_5_conv1d_weight[encoder_idx][1],
            model.encoder_4_5_dconv_layers_5_conv1d_bias[encoder_idx][1]);
        break;
    case 1:
        y = demucscpp::conv1d<384, 3072, 1, 1, 0, 1>(
            y, model.encoder_4_5_dconv_layers_5_conv1d_weight[encoder_idx][1],
            model.encoder_4_5_dconv_layers_5_conv1d_bias[encoder_idx][1]);
        break;
    };

    y = demucscpp::group_norm(
        y, model.encoder_4_5_dconv_layers_6_groupnorm_weight[encoder_idx][1],
        model.encoder_4_5_dconv_layers_6_groupnorm_bias[encoder_idx][1], 1,
        1e-05);

    y = demucscpp::glu(y, 1);

    y = demucscpp::layer_scale(
        y, model.encoder_4_5_dconv_layers_8_scale[encoder_idx][1]);

    // now sum with itself
    y = y + y_copy;
}

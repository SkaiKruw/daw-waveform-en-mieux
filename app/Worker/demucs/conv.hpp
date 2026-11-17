#ifndef CONV_HPP
#define CONV_HPP

#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>
#include <iostream>
#include <unsupported/Eigen/CXX11/Tensor>
#include <algorithm>
#include <vector>

namespace demucscpp
{

// --- Onde : im2col réécrits avec des accès mémoire contigus --------------------------------------------
// Les tenseurs Eigen sont en « column-major » : input(c, h, w) est à c + C * (h + H * w), donc parcourir w
// saute de C x H flottants. On recopie d'abord l'entrée en lignes contiguës [c][h][w] (une seule passe),
// puis chaque colonne de la matrice im2col est remplie séquentiellement. Résultat strictement identique.
inline const float *rows_chw(const Eigen::Tensor3dXf &input,
                             std::vector<float> &buf)
{
    const int C = (int)input.dimension(0), H = (int)input.dimension(1),
              W = (int)input.dimension(2);
    buf.resize((size_t)C * H * W);
    const float *in = input.data();
    for (int w = 0; w < W; ++w)
        for (int h = 0; h < H; ++h)
        {
            const float *src = in + (size_t)C * ((size_t)h + (size_t)H * w);
            for (int c = 0; c < C; ++c)
                buf[((size_t)c * H + h) * W + w] = src[c];
        }
    return buf.data();
}

template <int kernel_height, int kernel_width, int stride_height,
          int stride_width, int pad_height, int pad_width, int dilation_height,
          int dilation_width>
inline Eigen::MatrixXf im2col(const Eigen::Tensor3dXf &input)
{
    const int in_channels = (int)input.dimension(0);
    const int in_height = (int)input.dimension(1);
    const int in_width = (int)input.dimension(2);

    const int height_col = static_cast<int>(
        std::ceil((in_height + 2 * pad_height -
                   dilation_height * (kernel_height - 1) - 1) /
                  float(stride_height)) +
        1);
    const int width_col = static_cast<int>(
        std::ceil((in_width + 2 * pad_width -
                   dilation_width * (kernel_width - 1) - 1) /
                  float(stride_width)) +
        1);

    std::vector<float> buf;   // local : peut peser plus de 100 Mo, libéré aussitôt
    const float *rows = rows_chw(input, buf);

    Eigen::MatrixXf output(height_col * width_col,
                           in_channels * kernel_height * kernel_width);

    for (int c = 0; c < in_channels; c++)
        for (int kh = 0; kh < kernel_height; kh++)
            for (int kw = 0; kw < kernel_width; kw++)
            {
                float *col =
                    output.col(c * kernel_height * kernel_width +
                               kh * kernel_width + kw)
                        .data();
                for (int h = 0; h < height_col; h++)
                {
                    float *dst = col + (size_t)h * width_col;
                    const int h_pad =
                        h * stride_height + kh * dilation_height - pad_height;
                    if (h_pad < 0 || h_pad >= in_height)
                    {
                        std::fill(dst, dst + width_col, 0.0f);
                        continue;
                    }
                    const float *src =
                        rows + ((size_t)c * in_height + h_pad) * in_width;
                    for (int w = 0; w < width_col; w++)
                    {
                        const int w_pad =
                            w * stride_width + kw * dilation_width - pad_width;
                        dst[w] = (w_pad >= 0 && w_pad < in_width) ? src[w_pad]
                                                                  : 0.0f;
                    }
                }
            }

    return output;
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d(const Eigen::Tensor3dXf &x, const Eigen::Tensor4dXf &w,
                         const Eigen::Tensor1dXf &b)
{
    int in_height = x.dimension(1);
    int in_width = x.dimension(2);

    // Calculate output dimensions with the correct application of ceil
    int out_height =
        static_cast<int>(std::ceil(
            (float)(in_height + 2 * pad_height - (kernel_height - 1) - 1) /
            stride_height)) +
        1;
    int out_width =
        static_cast<int>(std::ceil(
            (float)(in_width + 2 * pad_width - (kernel_width - 1) - 1) /
            stride_width)) +
        1;

    // Apply im2col
    Eigen::MatrixXf im2col_matrix =
        im2col<kernel_height, kernel_width, stride_height, stride_width,
               pad_height, pad_width, dilation_height, dilation_width>(x);

    // Reshape weights
    // reverse last 3 axes (out chanel x in chan x kernel height x kernel width
    // -> out chan x (kernel width x kernel height x in chan))
    Eigen::Tensor4dXf w_swapped = w.shuffle(Eigen::array<int, 4>({0, 3, 2, 1}));
    // then flatten to the last axis
    Eigen::Tensor2dXf reshaped_weights_tensor =
        w_swapped.reshape(Eigen::array<int, 2>{
            out_channels, in_channels * kernel_width * kernel_height});
    Eigen::MatrixXf reshaped_weights = Eigen::Map<Eigen::MatrixXf>(
        reshaped_weights_tensor.data(), reshaped_weights_tensor.dimension(0),
        reshaped_weights_tensor.dimension(1));

    // Perform matrix multiplication with GEMM
    Eigen::MatrixXf result = im2col_matrix * reshaped_weights.transpose();

    // Add bias to each column of the result matrix
    for (int chout = 0; chout < out_channels; ++chout)
    {
        result.col(chout).array() += b(chout);
    }

    // Reshape result to 3D output tensor
    Eigen::Tensor3dXf y_out(out_channels, out_height, out_width);
    y_out.setZero();

    for (int chout = 0; chout < out_channels; ++chout)
    {
        for (int h = 0; h < out_height; ++h)
        {
            for (int w_ = 0; w_ < out_width; ++w_)
            {
                int row_idx = h * out_width + w_;
                // Assign the value from the GEMM output to the output tensor
                if (row_idx < result.rows())
                {
                    y_out(chout, h, w_) = result(row_idx, chout);
                }
            }
        }
    }

    return y_out;
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_fused_gelu(const Eigen::Tensor3dXf &x,
                                    const Eigen::Tensor4dXf &w,
                                    const Eigen::Tensor1dXf &b)
{
    int in_height = x.dimension(1);
    int in_width = x.dimension(2);

    // Calculate output dimensions with the correct application of ceil
    int out_height =
        static_cast<int>(std::ceil(
            (float)(in_height + 2 * pad_height - (kernel_height - 1) - 1) /
            stride_height)) +
        1;
    int out_width =
        static_cast<int>(std::ceil(
            (float)(in_width + 2 * pad_width - (kernel_width - 1) - 1) /
            stride_width)) +
        1;

    // Apply im2col
    Eigen::MatrixXf im2col_matrix =
        im2col<kernel_height, kernel_width, stride_height, stride_width,
               pad_height, pad_width, dilation_height, dilation_width>(x);

    // Reshape weights
    // reverse last 3 axes (out chanel x in chan x kernel height x kernel width
    // -> out chan x (kernel width x kernel height x in chan))
    Eigen::Tensor4dXf w_swapped = w.shuffle(Eigen::array<int, 4>({0, 3, 2, 1}));
    // then flatten to the last axis
    Eigen::Tensor2dXf reshaped_weights_tensor =
        w_swapped.reshape(Eigen::array<int, 2>{
            out_channels, in_channels * kernel_width * kernel_height});
    Eigen::MatrixXf reshaped_weights = Eigen::Map<Eigen::MatrixXf>(
        reshaped_weights_tensor.data(), reshaped_weights_tensor.dimension(0),
        reshaped_weights_tensor.dimension(1));

    // Perform matrix multiplication with GEMM
    Eigen::MatrixXf result = im2col_matrix * reshaped_weights.transpose();

    // Add bias to each column of the result matrix
    for (int chout = 0; chout < out_channels; ++chout)
    {
        result.col(chout).array() += b(chout);
    }

    // Reshape result to 3D output tensor
    Eigen::Tensor3dXf y_out(out_channels, out_height, out_width);
    y_out.setZero();

    for (int chout = 0; chout < out_channels; ++chout)
    {
        for (int h = 0; h < out_height; ++h)
        {
            for (int w_ = 0; w_ < out_width; ++w_)
            {
                int row_idx = h * out_width + w_;
                // Assign the value from the GEMM output to the output tensor
                // with gelu
                float value = result(row_idx, chout);
                float activated_value =
                    0.5f * value * (1.0f + std::erf(value / std::sqrt(2.0f)));
                // Assign the activated value to the output tensor
                y_out(chout, h, w_) = activated_value;
            }
        }
    }

    return y_out;
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d(const Eigen::Tensor3dXf &x, const Eigen::Tensor3dXf &w,
                         const Eigen::Tensor1dXf &b)
{
    // copy w into a 4d tensor with trailing (,1) dimension
    Eigen::Tensor4dXf w_4d = w.reshape(Eigen::array<int, 4>{
        {(int)w.dimension(0), (int)w.dimension(1), (int)w.dimension(2), 1}});

    // move 0 axis to the end
    Eigen::Tensor3dXf x_shuff = x.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // do 2d convolution inference here
    // treating the in_freq dimension as a width dimension with a no-op kernel
    Eigen::Tensor3dXf y_out =
        demucscpp::conv2d<in_channels, out_channels, kernel_size, 1, stride, 1,
                          pad, 0, dilation, 1>(x_shuff, w_4d, b);

    // move end axis to the front
    Eigen::Tensor3dXf y_out_shuf =
        y_out.shuffle(Eigen::array<int, 3>({2, 0, 1}));
    return y_out_shuf;
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_fused_gelu(const Eigen::Tensor3dXf &x,
                                    const Eigen::Tensor3dXf &w,
                                    const Eigen::Tensor1dXf &b)
{
    // copy w into a 4d tensor with trailing (,1) dimension
    Eigen::Tensor4dXf w_4d = w.reshape(Eigen::array<int, 4>{
        {(int)w.dimension(0), (int)w.dimension(1), (int)w.dimension(2), 1}});

    // move 0 axis to the end
    Eigen::Tensor3dXf x_shuff = x.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // do 2d convolution inference here
    // treating the in_freq dimension as a width dimension with a no-op kernel
    Eigen::Tensor3dXf y_out =
        demucscpp::conv2d_fused_gelu<in_channels, out_channels, kernel_size, 1,
                                     stride, 1, pad, 0, dilation, 1>(x_shuff,
                                                                     w_4d, b);

    // move end axis to the front
    Eigen::Tensor3dXf y_out_shuf =
        y_out.shuffle(Eigen::array<int, 3>({2, 0, 1}));
    return y_out_shuf;
}

template <int kernel_height, int kernel_width, int stride_height,
          int stride_width, int pad_height, int pad_width, int dilation_height,
          int dilation_width>
Eigen::MatrixXf im2col_transposed(const Eigen::Tensor3dXf &input)
{
    // Onde : même placement que l'original (chaque (h, w) d'entrée va à une position distincte de sa
    // colonne), avec lecture de l'entrée en lignes contiguës.
    const int channels = (int)input.dimension(0);
    const int input_height = (int)input.dimension(1);
    const int input_width = (int)input.dimension(2);

    const int effective_kernel_height =
        kernel_height + (kernel_height - 1) * (dilation_height - 1);
    const int effective_kernel_width =
        kernel_width + (kernel_width - 1) * (dilation_width - 1);
    const int expanded_height =
        (input_height - 1) * stride_height + effective_kernel_height;
    const int expanded_width =
        (input_width - 1) * stride_width + effective_kernel_width;

    std::vector<float> buf;   // local : peut peser plus de 100 Mo, libéré aussitôt
    const float *rows = rows_chw(input, buf);

    Eigen::MatrixXf output =
        Eigen::MatrixXf::Zero(expanded_height * expanded_width,
                              channels * kernel_height * kernel_width);

    for (int c = 0; c < channels; ++c)
        for (int kh = 0; kh < kernel_height; ++kh)
            for (int kw = 0; kw < kernel_width; ++kw)
            {
                float *col = output.col(c * kernel_height * kernel_width +
                                        kh * kernel_width + kw)
                                 .data();
                for (int h = 0; h < input_height; ++h)
                {
                    const int eh =
                        h * stride_height + kh * dilation_height - pad_height;
                    if (eh < 0 || eh >= expanded_height)
                        continue;
                    const float *src =
                        rows + ((size_t)c * input_height + h) * input_width;
                    float *dst = col + (size_t)eh * expanded_width;
                    for (int w = 0; w < input_width; ++w)
                    {
                        const int ew =
                            w * stride_width + kw * dilation_width - pad_width;
                        if (ew >= 0 && ew < expanded_width)
                            dst[ew] = src[w];
                    }
                }
            }

    return output;
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_tr(const Eigen::Tensor3dXf &x,
                            const Eigen::Tensor4dXf &w,
                            const Eigen::Tensor1dXf &b)
{
    int in_height = x.dimension(1);
    int in_width = x.dimension(2);

    int effective_kernel_height =
        kernel_height + (kernel_height - 1) * (dilation_height - 1);
    int effective_kernel_width =
        kernel_width + (kernel_width - 1) * (dilation_width - 1);

    int out_height = (in_height - 1) * stride_height + effective_kernel_height -
                     2 * pad_height;
    int out_width =
        (in_width - 1) * stride_width + effective_kernel_width - 2 * pad_width;

    //  Apply an adapted im2col for transposed convolution
    Eigen::MatrixXf im2col_matrix =
        im2col_transposed<kernel_height, kernel_width, stride_height,
                          stride_width, pad_height, pad_width, dilation_height,
                          dilation_width>(x);

    //  Reshape and prepare the weights as in conv2d_gemm
    //  keeping in mind transpose weights are stored as (Cin, Cout, Kh, Kw) (not
    //  Cout, Cin, Kh, Kw)
    Eigen::Tensor4dXf w_swapped = w.shuffle(Eigen::array<int, 4>(
        {1, 3, 2, 0})); // Note the change in the shuffle order
    Eigen::Tensor2dXf reshaped_weights_tensor =
        w_swapped.reshape(Eigen::array<int, 2>{
            out_channels, in_channels * kernel_height * kernel_width});
    Eigen::MatrixXf reshaped_weights = Eigen::Map<Eigen::MatrixXf>(
        reshaped_weights_tensor.data(), reshaped_weights_tensor.dimension(0),
        reshaped_weights_tensor.dimension(1));

    // Perform matrix multiplication with GEMM
    Eigen::MatrixXf result = im2col_matrix * reshaped_weights.transpose();

    // Add bias to result
    for (int chout = 0; chout < out_channels; ++chout)
    {
        result.col(chout).array() += b(chout);
    }

    Eigen::Tensor3dXf y_out(out_channels, out_height, out_width);
    y_out.setZero();

    // Reshape and sum up the contributions to the output
    for (int ch = 0; ch < out_channels; ++ch)
    {
        for (int h = 0; h < out_height; ++h)
        {
            for (int w_ = 0; w_ < out_width; ++w_)
            {
                // Calculate the linear index in the GEMM result corresponding
                // to this output location
                int gemm_row = h * out_width + w_;
                int gemm_col = ch;

                // Assign the value from the GEMM result to the output tensor
                y_out(ch, h, w_) += result(gemm_row, gemm_col);
            }
        }
    }

    return y_out;
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_tr_fused_gelu(const Eigen::Tensor3dXf &x,
                                       const Eigen::Tensor4dXf &w,
                                       const Eigen::Tensor1dXf &b)
{
    int in_height = x.dimension(1);
    int in_width = x.dimension(2);

    int effective_kernel_height =
        kernel_height + (kernel_height - 1) * (dilation_height - 1);
    int effective_kernel_width =
        kernel_width + (kernel_width - 1) * (dilation_width - 1);

    int out_height = (in_height - 1) * stride_height + effective_kernel_height -
                     2 * pad_height;
    int out_width =
        (in_width - 1) * stride_width + effective_kernel_width - 2 * pad_width;

    //  Apply an adapted im2col for transposed convolution
    Eigen::MatrixXf im2col_matrix =
        im2col_transposed<kernel_height, kernel_width, stride_height,
                          stride_width, pad_height, pad_width, dilation_height,
                          dilation_width>(x);

    //  Reshape and prepare the weights as in conv2d_gemm
    //  keeping in mind transpose weights are stored as (Cin, Cout, Kh, Kw) (not
    //  Cout, Cin, Kh, Kw)
    Eigen::Tensor4dXf w_swapped = w.shuffle(Eigen::array<int, 4>(
        {1, 3, 2, 0})); // Note the change in the shuffle order
    Eigen::Tensor2dXf reshaped_weights_tensor =
        w_swapped.reshape(Eigen::array<int, 2>{
            out_channels, in_channels * kernel_height * kernel_width});
    Eigen::MatrixXf reshaped_weights = Eigen::Map<Eigen::MatrixXf>(
        reshaped_weights_tensor.data(), reshaped_weights_tensor.dimension(0),
        reshaped_weights_tensor.dimension(1));

    // Perform matrix multiplication with GEMM
    Eigen::MatrixXf result = im2col_matrix * reshaped_weights.transpose();

    // Add bias to result
    for (int chout = 0; chout < out_channels; ++chout)
    {
        result.col(chout).array() += b(chout);
    }

    Eigen::Tensor3dXf y_out(out_channels, out_height, out_width);
    y_out.setZero();

    // Reshape and sum up the contributions to the output
    for (int ch = 0; ch < out_channels; ++ch)
    {
        for (int h = 0; h < out_height; ++h)
        {
            for (int w_ = 0; w_ < out_width; ++w_)
            {
                // Calculate the linear index in the GEMM result corresponding
                // to this output location
                int gemm_row = h * out_width + w_;
                int gemm_col = ch;

                // Compute the value from the GEMM result
                float value = result(gemm_row, gemm_col);

                // Apply GeLU activation
                float activated_value =
                    0.5f * value * (1.0f + std::erf(value / std::sqrt(2.0f)));

                // Assign the activated value to the output tensor
                y_out(ch, h, w_) += activated_value;
            }
        }
    }

    return y_out;
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_tr(const Eigen::Tensor3dXf &x,
                            const Eigen::Tensor3dXf &w,
                            const Eigen::Tensor1dXf &b)
{
    // Convert 1D convolution to 2D convolution by adding an extra dimension
    Eigen::Tensor4dXf w_4d = w.reshape(Eigen::array<int, 4>{
        {(int)w.dimension(0), (int)w.dimension(1), (int)w.dimension(2), 1}});

    // Move 0 axis to the end
    Eigen::Tensor3dXf x_shuff = x.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // Call the 2D transposed convolution function
    Eigen::Tensor3dXf y_out =
        conv2d_tr<in_channels, out_channels, kernel_size, 1, stride, 1, pad, 0,
                  dilation, 1>(x_shuff, w_4d, b);

    // Move end axis to the front
    Eigen::Tensor3dXf y_out_shuf =
        y_out.shuffle(Eigen::array<int, 3>({2, 0, 1}));

    return y_out_shuf;
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_tr_fused_gelu(const Eigen::Tensor3dXf &x,
                                       const Eigen::Tensor3dXf &w,
                                       const Eigen::Tensor1dXf &b)
{
    // Convert 1D convolution to 2D convolution by adding an extra dimension
    Eigen::Tensor4dXf w_4d = w.reshape(Eigen::array<int, 4>{
        {(int)w.dimension(0), (int)w.dimension(1), (int)w.dimension(2), 1}});

    // Move 0 axis to the end
    Eigen::Tensor3dXf x_shuff = x.shuffle(Eigen::array<int, 3>({1, 2, 0}));

    // Call the 2D transposed convolution function
    Eigen::Tensor3dXf y_out =
        conv2d_tr_fused_gelu<in_channels, out_channels, kernel_size, 1, stride,
                             1, pad, 0, dilation, 1>(x_shuff, w_4d, b);

    // Move end axis to the front
    Eigen::Tensor3dXf y_out_shuf =
        y_out.shuffle(Eigen::array<int, 3>({2, 0, 1}));

    return y_out_shuf;
}

} // namespace demucscpp

#endif // CONV_HPP

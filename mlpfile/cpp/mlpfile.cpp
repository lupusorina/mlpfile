#include <cstdio>
#include <memory>

#include "mlpfile.h"

namespace mlpfile
{
	static std::string const layer_type_names[] = {
		"Reserved",
		"Input",
		"Linear",
		"ReLU",
	};

	std::string Layer::describe() const
	{
		std::string s = layer_type_names[type];
		switch (type) {
		case mlpfile::Input:
			s += ": " + std::to_string(input_size);
			break;
		case mlpfile::Linear:
			s += ": " + std::to_string(W.cols())
				+ " -> " + std::to_string(W.rows());
			break;
		default:
			break;
		}
		return s;
	}

	Model Model::load(char const *path)
	{
		Model model;

		FILE *fp = fopen(path, "r");
		if (!fp) {
			throw std::runtime_error("Could not open file.");
		}

		auto readu32 = [fp]()
		{
			uint32_t u32;
			size_t r = fread(&u32, sizeof(u32), 1, fp);
			if (r != 1) {
				throw std::runtime_error("File format error.");
			}
			return u32;
		};

		uint32_t n_layers = readu32();
		model.layers.resize(n_layers);

		uint32_t size = 0;

		// Pass 1: Metadata
		for (int i = 0; i < n_layers; ++i) {
			Layer &layer = model.layers[i];
			layer.type = (LayerType)readu32();
			if (layer.type == Input) {
				if (size != 0) {
					throw std::runtime_error("Input layer appeared in wrong place.");
				}
				size = readu32();
				if (size == 0) {
					throw std::runtime_error("Input size 0 is not valid.");
				}
				layer.input_size = size;
			}
			else if (layer.type == Linear) {
				if (size == 0) {
					throw std::runtime_error("Linear layer appeared before Input.");
				}
				uint32_t next_size = readu32();
				if (next_size == 0) {
					throw std::runtime_error("Linear layer output size 0 is not valid.");
				}
				layer.W = MatrixXfRow(next_size, size);
				layer.b = Eigen::VectorXf(next_size);
				size = next_size;
			}
			else if (layer.type == ReLU) {
				// do nothing.
			}
		}

		// Pass 2: Data
		for (int i = 0; i < n_layers; ++i) {
			if (model.layers[i].type == Linear) {
				Layer &layer = model.layers[i];
				uint32_t total = layer.W.rows() * layer.W.cols();
				size_t rW = fread(&layer.W(0, 0), sizeof(float), total, fp);
				if (rW != total) {
					throw std::runtime_error("Not enough data in file.");
				}
				size_t rb = fread(&layer.b[0], sizeof(float), layer.W.rows(), fp);
				if (rb != layer.W.rows()) {
					throw std::runtime_error("Not enough data in file.");
				}
			}
		}

		// now the file should be empty
		char dummy;
		size_t rend = fread(&dummy, 1, 1, fp);
		if (rend != 0) {
			throw std::runtime_error("More data than expected at end of file.");
		}

		return model;
	}

	Eigen::VectorXf Model::forward(Eigen::VectorXf x)
	{
		for (Layer const &layer : layers) {
			if (layer.type == mlpfile::Input) {
				if (layer.input_size != x.rows()) {
					throw std::runtime_error("incorrect input size");
				}
			}
			else if (layer.type == mlpfile::Linear) {
				x = layer.W * x + layer.b;
			}
			else if (layer.type == mlpfile::ReLU) {
				x = x.array().max(0);
			}
			else {
				throw std::runtime_error("unrecognized type");
			}
		}
		return x;
	}

	MatrixXfRow Model::jacobian(Eigen::VectorXf const &x)
	{
		std::unique_ptr<MatrixXfRow> J;

		std::vector<Eigen::VectorXf> stack = { x };

		// Forward pass
		for (Layer const &layer : layers) {
			if (layer.type == mlpfile::Input) {
				if (layer.input_size != x.rows()) {
					throw std::runtime_error("incorrect input size");
				}
			}
			else if (layer.type == mlpfile::Linear) {
				stack.push_back(layer.W * stack.back() + layer.b);
			}
			else if (layer.type == mlpfile::ReLU) {
				stack.push_back(stack.back().array().max(0));
			}
			else {
				throw std::runtime_error("unrecognized type");
			}
		}

		// Backward pass
		for (int i = (int)layers.size() - 1; i >= 0; --i) {
			if (layers[i].type == mlpfile::ReLU) {
				// ReLU
				assert (J != nullptr);
				Eigen::VectorXf const &f = stack.back();
				// TODO: Would it be possible to do this without a loop, like
				// we can in NumPy by using broadcasting?
				for (int j = 0; j < f.rows(); ++j) {
					assert (f[j] >= 0.0f);
					if (f[j] == 0.0f) {
						J->col(j) *= 0.0f;
					}
				}
			}
			else if (layers[i].type == mlpfile::Linear) {
				if (J == nullptr) {
					J.reset(new MatrixXfRow(layers[i].W));
				}
				else {
					*J = (*J) * layers[i].W;
				}
			}
			else if (layers[i].type == mlpfile::Input) {
				// TODO: validate size
			}
			else {
				throw std::runtime_error("unrecognized type");
			}
			stack.pop_back();
		}
		assert (stack.size() == 0);
		return *J;
	}

	std::string Model::describe() const
	{
		return "mlpfile::Model with " + std::to_string(layers.size()) + " Layers";
	}

}  // namespace mlpfile
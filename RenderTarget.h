#pragma once

namespace Core
{
	class Texture;
	class RenderTarget
	{
	public:
		using CreateFunc = function<unique_ptr<RenderTarget>(Texture&&)>;

		static const CreateFunc DEFAULT_CREATE_FUNC;

		RenderTarget(std::vector<Texture>&& images);
		RenderTarget(const RenderTarget&) = delete;
		RenderTarget(RenderTarget&&) = delete;

		RenderTarget& operator=(const RenderTarget& other) noexcept = delete;
		RenderTarget& operator=(RenderTarget&& other) noexcept = delete;

		const VkExtent2D& GetExtent() const;

		const std::vector<core::ImageView>& get_views() const;

		const std::vector<Attachment>& get_attachments() const;

		/**
		 * @brief Sets the current input attachments overwriting the current ones
		 *        Should be set before beginning the render pass and before starting a new subpass
		 * @param input Set of attachment reference number to use as input
		 */
		void set_input_attachments(std::vector<uint32_t>& input);

		const std::vector<uint32_t>& get_input_attachments() const;

		/**
		 * @brief Sets the current output attachments overwriting the current ones
		 *        Should be set before beginning the render pass and before starting a new subpass
		 * @param output Set of attachment reference number to use as output
		 */
		void set_output_attachments(std::vector<uint32_t>& output);

		const std::vector<uint32_t>& get_output_attachments() const;

		void set_layout(uint32_t attachment, VkImageLayout layout);

		VkImageLayout get_layout(uint32_t attachment) const;

	private:
		Device const& device;

		VkExtent2D extent{};

		std::vector<core::Image> images;

		std::vector<core::ImageView> views;

		std::vector<Attachment> attachments;

		/// By default there are no input attachments
		std::vector<uint32_t> input_attachments = {};

		/// By default the output attachments is attachment 0
		std::vector<uint32_t> output_attachments = { 0 };
	};
}


#pragma once

namespace Core
{
	class Device;
	class IDescriptor;
	class IPushConstant;
	class Material
	{
	public:
		Material(Device& device);
		virtual ~Material();

		void SetShader(const string& vertFilePath,
			const string& fragFilePath);
		IDescriptor* GetDescriptor() const;
		IPushConstant* GetPushConstant() const;

		size_t GetShaderId() const
		{
			return _shaderId;
		}
	private:
		vector<char> ReadFile(const string& filePath);
	private:
		Device& _device;
		size_t _shaderId;

		//todo : ���̴�
		//todo : ubo ���
		//todo : push constant ���
	};
}


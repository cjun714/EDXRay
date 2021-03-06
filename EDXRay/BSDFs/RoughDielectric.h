#pragma once

#include "../Core/BSDF.h"

namespace EDX
{
	namespace RayTracer
	{
		class RoughDielectric : public BSDF
		{
		private:
			UniquePtr<Texture2D<float>> mRoughness;
			float mEtai, mEtat;

			static const ScatterType ReflectScatter = ScatterType(BSDF_REFLECTION | BSDF_GLOSSY);
			static const ScatterType RefractScatter = ScatterType(BSDF_TRANSMISSION | BSDF_GLOSSY);

		public:
			RoughDielectric(const Color& reflectance = Color::WHITE, float roughness = 0.06f, float etai = 1.0f, float etat = 1.5f)
				: BSDF(ScatterType(BSDF_REFLECTION | BSDF_TRANSMISSION | BSDF_GLOSSY), BSDFType::RoughDielectric, reflectance)
				, mRoughness(new ConstantTexture2D<float>(roughness))
				, mEtai(etai)
				, mEtat(etat)
			{
			}
			RoughDielectric(UniquePtr<Texture2D<Color>> pTex, UniquePtr<Texture2D<Color>> pNormal, float roughness = 0.06f, float etai = 1.0f, float etat = 1.5f)
				: BSDF(ScatterType(BSDF_REFLECTION | BSDF_TRANSMISSION | BSDF_GLOSSY), BSDFType::RoughDielectric, Move(pTex), Move(pNormal))
				, mRoughness(new ConstantTexture2D<float>(roughness))
				, mEtai(etai)
				, mEtat(etat)
			{
			}
			RoughDielectric(const char* pFile, float roughness = 0.05f, float etai = 1.0f, float etat = 1.5f)
				: BSDF(ScatterType(BSDF_REFLECTION | BSDF_TRANSMISSION | BSDF_GLOSSY), BSDFType::RoughDielectric, pFile)
				, mRoughness(new ConstantTexture2D<float>(roughness))
				, mEtai(etai)
				, mEtat(etat)
			{
			}

			Color SampleScattered(const Vector3& _wo,
				const Sample& sample,
				const DifferentialGeom& diffGeom,
				Vector3* pvIn, float* pPdf,
				ScatterType types = BSDF_ALL,
				ScatterType* pSampledTypes = NULL) const;

		private:
			float Pdf(const Vector3& vOut, const Vector3& vIn, const DifferentialGeom& diffGeom, ScatterType types /* = BSDF_ALL */) const
			{
				Vector3 vWo = diffGeom.WorldToLocal(vOut);
				Vector3 vWi = diffGeom.WorldToLocal(vIn);

				return PdfInner(vWo, vWi, diffGeom, types);
			}

			float PdfInner(const Vector3& wo, const Vector3& wi, const DifferentialGeom& diffGeom, ScatterType types = BSDF_ALL) const override
			{
				bool sampleReflect = (types & ReflectScatter) == ReflectScatter;
				bool sampleRefract = (types & RefractScatter) == RefractScatter;
				const float ODotN = BSDFCoordinate::CosTheta(wo), IDotN = BSDFCoordinate::CosTheta(wi);
				const float fac = ODotN * IDotN;
				if (fac == 0.0f)
					return 0.0f;

				bool reflect = fac > 0.0f;
				bool entering = ODotN > 0.0f;

				Vector3 wh;
				float dwh_dwi;
				if (reflect)
				{
					if (!sampleReflect)
						return 0.0f;

					wh = Math::Normalize(wo + wi);
					//if (!entering)
					//	wh *= -1.0f;

					dwh_dwi = 1.0f / (4.0f * Math::Dot(wi, wh));
				}
				else
				{
					if (!sampleRefract)
						return 0.0f;

					float etai = mEtai, etat = mEtat;
					if (!entering)
						Swap(etai, etat);

					wh = -Math::Normalize(etai * wo + etat * wi);

					const float ODotH = Math::Dot(wo, wh), IDotH = Math::Dot(wi, wh);
					float sqrtDenom = etai * ODotH + etat * IDotH;
					dwh_dwi = (etat * etat * IDotH) / (sqrtDenom * sqrtDenom);
				}

				wh *= Math::Sign(BSDFCoordinate::CosTheta(wh));

				float roughness = GetValue(mRoughness.Get(), diffGeom, TextureFilter::Linear);
				roughness = Math::Clamp(roughness, 0.02f, 1.0f);
				float whProb = GGX_Pdf_VisibleNormal(Math::Sign(BSDFCoordinate::CosTheta(wo)) * wo, wh, roughness * roughness);
				if (sampleReflect && sampleRefract)
				{
					float F = BSDF::FresnelDielectric(Math::Dot(wo, wh), mEtai, mEtat);
					//F = 0.5f * F + 0.25f;
					whProb *= reflect ? F : 1.0f - F;
				}

				Assert(Math::NumericValid(whProb));
				Assert(Math::NumericValid(dwh_dwi));
				return Math::Abs(whProb * dwh_dwi);
			}

			Color Eval(const Vector3& vOut, const Vector3& vIn, const DifferentialGeom& diffGeom, ScatterType types) const
			{
				Vector3 vWo = diffGeom.WorldToLocal(vOut);
				Vector3 vWi = diffGeom.WorldToLocal(vIn);

				return GetValue(mpTexture.Get(), diffGeom) * EvalInner(vWo, vWi, diffGeom, types);
			}

			float EvalInner(const Vector3& wo, const Vector3& wi, const DifferentialGeom& diffGeom, ScatterType types = BSDF_ALL) const override
			{
				const float ODotN = BSDFCoordinate::CosTheta(wo), IDotN = BSDFCoordinate::CosTheta(wi);
				const float fac = ODotN * IDotN;
				if (fac == 0.0f)
					return 0.0f;

				bool reflect = fac > 0.0f;
				bool entering = ODotN > 0.0f;

				float etai = mEtai, etat = mEtat;
				if (!entering)
					Swap(etai, etat);

				Vector3 wh;
				if (reflect)
				{
					if ((ReflectScatter & types) != ReflectScatter)
						return 0.0f;

					wh = wo + wi;
					if (wh == Vector3::ZERO)
						return 0.0f;

					wh = Math::Normalize(wh);
				}
				else
				{
					if ((RefractScatter & types) != RefractScatter)
						return 0.0f;

					wh = -Math::Normalize(etai * wo + etat * wi);
				}

				wh *= Math::Sign(BSDFCoordinate::CosTheta(wh));

				float roughness = GetValue(mRoughness.Get(), diffGeom, TextureFilter::Linear);
				roughness = Math::Clamp(roughness, 0.02f, 1.0f);
				float sampleRough = roughness * roughness;

				float D = GGX_D(wh, sampleRough);
				if (D == 0.0f)
					return 0.0f;

				float F = BSDF::FresnelDielectric(Math::Dot(wo, wh), mEtai, mEtat);
				float G = GGX_G(wo, wi, wh, sampleRough);

				if (reflect)
				{
					return Math::Abs(F * D * G / (4.0f * BSDFCoordinate::CosTheta(wi) * BSDFCoordinate::CosTheta(wo)));
				}
				else
				{
					const float ODotH = Math::Dot(wo, wh), IDotH = Math::Dot(wi, wh);

					float sqrtDenom = etai * ODotH + etat * IDotH;
					float value = ((1 - F) * D * G * etat * etat * ODotH * IDotH) /
						(sqrtDenom * sqrtDenom * ODotN * IDotN);

					// TODO: Fix solid angle compression when tracing radiance
					float factor = 1.0f;

					Assert(Math::NumericValid(value));
					return Math::Abs(value * factor * factor);
				}
			}

		public:
			int GetParameterCount() const
			{
				return BSDF::GetParameterCount() + 2;
			}

			String GetParameterName(const int idx) const
			{
				if (idx < BSDF::GetParameterCount())
					return BSDF::GetParameterName(idx);

				int baseParamCount = BSDF::GetParameterCount();
				if (idx == baseParamCount + 0)
					return "Roughness";
				else if (idx == baseParamCount + 1)
					return "IOR";

				return "";
			}

			Parameter GetParameter(const String& name) const
			{
				Parameter ret = BSDF::GetParameter(name);
				if (ret.Type != Parameter::None)
					return ret;

				if (name == "Roughness")
				{
					ret.Type = Parameter::Float;
					ret.Value = this->mRoughness->GetValue();
					ret.Min = 0.01f;
					ret.Max = 1.0f;

					return ret;
				}
				else if (name == "IOR")
				{
					ret.Type = Parameter::Float;
					ret.Value = this->mEtat;
					ret.Min = 1.0f + 1e-4f;
					ret.Max = 1.8f;

					return ret;
				}

				return ret;
			}

			void SetParameter(const String& name, const Parameter& param)
			{
				BSDF::SetParameter(name, param);

				if (name == "Roughness")
				{
					if (param.Type == Parameter::Float)
					{
						if (this->mRoughness->IsConstant())
							this->mRoughness->SetValue(param.Value);
						else
							this->mRoughness.Reset(new ConstantTexture2D<float>(param.Value));
					}
					else if (param.Type == Parameter::TextureMap)
						this->mRoughness.Reset(new ImageTexture<float, float>(param.TexPath, 1.0f));
				}
				else if (name == "IOR")
					this->mEtat = param.Value;

				return;
			}
		};
	}
}
#include "batchComponent.h"
#include <string>
#include <coreModule/graphicsWrapper.hpp>

#include "transform.h"
#include "coreModule/ecs/object.h"
#include "renderModule/backends/gl/materialGl.h"
#include "renderModule/backends/gl/meshGl.h"
#include "renderModule/backends/gl/modelGl.h"
#include "renderModule/backends/gl/textureGl.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/fileSystem/fileSystem.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

BatchComponent::BatchComponent(UTILS::Ref<ECS::Object> obj) : ComponentBase(obj) {
	__NAME__ = "BatchComponent";
#ifdef OPENGL_BACKEND
	init();
#endif
}

BatchComponent::Descriptor BatchComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<BatchComponent>();
	return descriptor;
}
#ifdef OPENGL_BACKEND
void BatchComponent::init() {
	auto model = obj->getComponent<ModelRenderer>();
	auto material = obj->getComponent<MaterialRenderer>();

	CreateAtlases(*material.get());
	createBuffers(*model.get(), *material.get());
}
//------------------------------------
struct Rect
{
	float x = 0;
	float y = 0;
	float w = 0;
	float h = 0;

	Rect() :
		x(0),
		y(0),
		w(0.0f),
		h(0.0f)
	{

	}

	Rect(float _x, float _y, float _w, float _h)
	{
		x = _x;
		y = _y;
		w = _w;
		h = _h;
	}
};

struct Slot
{
	int id;
	int x;
	int y;
	Rect rect;
	bool vacant;
	bool checkedSlot;

	void Occupy()
	{
		vacant = false;
	}

	void Checked()
	{
		checkedSlot = true;
	}

	void Uncheck()
	{
		checkedSlot = false;
	}
};

struct TexturePair
{
	std::shared_ptr<RENDER::TextureGl> texID;
	Rect rect;
	bool checkedPlacement = false;
	bool placed = false;

	TexturePair() :
		rect(0, 0, 0, 0),
		checkedPlacement(false),
		placed(false)
	{

	}

	TexturePair(std::shared_ptr<RENDER::TextureGl> texID, Rect rect) :
		texID(texID),
		rect(rect),
		checkedPlacement(false),
		placed(false)
	{

	}
};

struct TextureStPair
{
	std::shared_ptr<RENDER::TextureGl> texID;
	Rect uvRect;//Texcoord
	//Rect stRect;//Normalized texCoord

	TextureStPair(std::shared_ptr<RENDER::TextureGl> _texID, Rect _uvRect) :
		texID(_texID),
		uvRect(_uvRect)
	{

	}
};

class SlotBundle
{
private:
	Slot currentSlot;
	std::vector<Slot> neighbourSlots;
	std::shared_ptr<RENDER::TextureGl> texture;
	Rect rect;//0 <-> AtlasWidth

public:
	void AddCurrentSlot(Slot slot)
	{
		currentSlot = slot;
	}
	void AddNeighbourSlot(Slot slot)
	{
		neighbourSlots.push_back(slot);
	}
	Slot GetCurrentSlot()
	{
		return currentSlot;
	}
	std::vector<Slot> GetNeighbourSlots()
	{
		return neighbourSlots;
	}
	void ClearNeighbourSlots()
	{
		neighbourSlots.clear();
	}

	void SetTexture(std::shared_ptr<RENDER::TextureGl> _texture)
	{
		texture = _texture;
	}

	const std::shared_ptr<RENDER::TextureGl> GetTexture() const
	{
		return texture;
	}

	//Normalized rect for texture to be placed in Atlas 
	void CalculateRectForTexture(float textureWidth, float textureHeight, float atlasWidth, float atlasHeight)
	{
		rect = Rect(currentSlot.rect.x, currentSlot.rect.y, textureWidth, textureHeight);
	}

	const Rect GetRect() const
	{
		return rect;
	}
};

struct AtlasSizeAndTextureRectPair
{
private:
	MATH::Vector2f atlasSize;
	Rect rect;
public:
	AtlasSizeAndTextureRectPair(MATH::Vector2f _textureAtlasSize, Rect _rect)
	{
		atlasSize = _textureAtlasSize;
		rect = _rect;
	}
	const MATH::Vector2f GetAtlasSize()
	{
		return atlasSize;
	}
	const Rect GetRect()
	{
		return rect;
	}
};
//------------------------------------
class TextureAtlas
{
public:
	void GetSmallestTextureSize();
	std::shared_ptr<RENDER::TextureGl> mAtlasTexture;
public:
	TextureAtlas();
	TextureAtlas(float width, float height, std::vector<TexturePair> texturePairs);
	~TextureAtlas();

	void OccupySlotsForTexture(TexturePair texturePair);
	void UncheckAllSlots();

	void AddTextureToTextureStPairs(TextureStPair textureStPair);
	void CreateTextureAtlasTexture();

	const std::shared_ptr<RENDER::TextureGl> GetTexture() const
	{
		return mAtlasTexture;
	}
	const AtlasSizeAndTextureRectPair GetAtlasSizeAndTextureRectPair(uint32_t texID) const;
private:
	Slot* GetSlot(uint32_t x, uint32_t y);
	Slot* GetNextVacantSlotIndex();
	void RemoveSlotFromAvailableSlots(uint32_t slotIndex);

	void AddTextureToAtlas(Rect rect, int channels, std::vector<unsigned char> texData, std::vector<GLubyte>& atlasData);
	GLuint CreateTextureForAtlas(std::vector<SlotBundle> slotBundles, float atlasWidth, float atlasHeight);
private:
	float mAtlasWidth = 0.0f;
	float mAtlasHeight = 0.0f;

	float mSmallestTextureWidth = 0.0f;
	float mSmallestTextureHeight = 0.0f;

	int mNumHorizontalSlots = 0;
	int mNumVerticalSlots = 0;

	SlotBundle mCurrentSlotBundle;

	std::vector<Slot> mAvailableSlots;
	std::vector<SlotBundle> mSlotBundles;
	std::unordered_map<uint32_t, SlotBundle> mSlotBundleMap;
	std::vector<TexturePair> mTexturePairs;
	std::vector<TextureStPair> mTextureStPairs;
};

TextureAtlas::TextureAtlas() :
	mAtlasWidth(0),
	mAtlasHeight(0),
	mNumHorizontalSlots(0),
	mNumVerticalSlots(0),
	mSmallestTextureWidth(0),
	mSmallestTextureHeight(0)
{

}

TextureAtlas::TextureAtlas(float atlasWidth, float atlasHeight, std::vector<TexturePair> texturePairs)
{
	mAtlasWidth = atlasWidth;
	mAtlasHeight = atlasHeight;
	mTexturePairs = texturePairs;

	mSmallestTextureWidth = atlasWidth;//Set smallestTextureWidth equal to textureAtlasWidth. It will be recalculated and reset later
	mSmallestTextureHeight = atlasHeight;//Set smallestTextureheight equal to textureAtlasHeight. It will be recalculated and reset later

	GetSmallestTextureSize();

	mNumHorizontalSlots = mAtlasWidth / mSmallestTextureWidth;
	mNumVerticalSlots = mAtlasHeight / mSmallestTextureHeight;

	//Slot creation
	for (uint32_t y = 0; y < mNumVerticalSlots; y++)
	{
		for (uint32_t x = 0; x < mNumHorizontalSlots; x++)
		{
			uint32_t id = x + y * mNumHorizontalSlots;
			Rect rect = Rect(x * mSmallestTextureWidth, y * mSmallestTextureHeight, mSmallestTextureWidth, mSmallestTextureHeight);

			Slot slot = Slot();
			slot.id = id;
			slot.x = x;
			slot.y = y;
			slot.rect = rect;
			slot.vacant = true;
			slot.checkedSlot = false;

			mAvailableSlots.push_back(slot);
		}
	}
}

TextureAtlas::~TextureAtlas()
{

}

void TextureAtlas::GetSmallestTextureSize() {
	for (auto& texturePair : mTexturePairs) {
		if (texturePair.texID->getWidth() < mSmallestTextureWidth)
			mSmallestTextureWidth = texturePair.texID->getWidth();

		if (texturePair.texID->getHeight() < mSmallestTextureHeight)
			mSmallestTextureHeight = texturePair.texID->getHeight();
	}
}

void TextureAtlas::OccupySlotsForTexture(TexturePair texturePair) {

	Slot* currentVacantSlotPtr = GetNextVacantSlotIndex();
	mCurrentSlotBundle.AddCurrentSlot(*currentVacantSlotPtr);

	//if (!currentVacantSlotPtr)
	//	TS_CORE_ASSERT("Could not find a vacant slot");

	currentVacantSlotPtr = nullptr;
	delete currentVacantSlotPtr;

	mCurrentSlotBundle.GetCurrentSlot().Checked();

	int requiredHorizontalSlots = texturePair.texID->getWidth() / mSmallestTextureWidth;
	int requiredVerticalSlots = texturePair.texID->getHeight() / mSmallestTextureHeight;

	//First check texture can't be placed in Atlas condition
	if (mCurrentSlotBundle.GetCurrentSlot().rect.x + texturePair.texID->getWidth() > mAtlasWidth &&
		mCurrentSlotBundle.GetCurrentSlot().rect.y + texturePair.texID->getHeight() > mAtlasHeight)
	{
		texturePair.checkedPlacement = true;
		texturePair.placed = false;
	}
	else
	{
		if (mCurrentSlotBundle.GetCurrentSlot().rect.x + texturePair.texID->getWidth() <= (float)mAtlasWidth)
		{
			for (uint32_t y = 0; y < requiredVerticalSlots; y++)
			{
				for (uint32_t x = 0; x < requiredHorizontalSlots; x++)
				{
					Slot* neighbourSlotToOccupyPtr = GetSlot(mCurrentSlotBundle.GetCurrentSlot().x + x, mCurrentSlotBundle.GetCurrentSlot().y + y);
					Slot neighbourSlotToOccupy = *neighbourSlotToOccupyPtr;

					if (neighbourSlotToOccupyPtr != nullptr)
					{
						if (neighbourSlotToOccupy.id != mCurrentSlotBundle.GetCurrentSlot().id)
						{
							neighbourSlotToOccupy.Checked();
							mCurrentSlotBundle.AddNeighbourSlot(neighbourSlotToOccupy);

							if (!neighbourSlotToOccupy.vacant)
							{
								//("NeighbourSlotToOccupy at index {0} is not vacant!", neighbourSlotToOccupy.id);

								mCurrentSlotBundle.ClearNeighbourSlots();

								OccupySlotsForTexture(texturePair);
								break;
							}
						}
					}
					else//Out side atlas range
					{
						//("NeighbourSlotToOccupy is outside range!");
						OccupySlotsForTexture(texturePair);
						break;
					}
				}

				if (texturePair.checkedPlacement)
				{
					//("Placement checked for texture with path {0}", Texture2D::GetTextureFromID(texturePair.texID)->GetPath());
					return;
				}
			}

			//Very important for keeping the texture and ST rect ready for adding the pixels to atlas later
			mCurrentSlotBundle.SetTexture(texturePair.texID);

			mCurrentSlotBundle.CalculateRectForTexture(
				texturePair.texID->getWidth(),
				texturePair.texID->getHeight(),
				mAtlasWidth,
				mAtlasHeight);

			mSlotBundles.push_back(mCurrentSlotBundle);
			mSlotBundleMap[mCurrentSlotBundle.GetTexture()->id] = mCurrentSlotBundle;

			if (!texturePair.checkedPlacement && !texturePair.placed)
			{
				//If all the checks are done, occupy slot and the valid neighbouring slots
				mCurrentSlotBundle.GetCurrentSlot().Occupy();
				//AddToFilledSlots(mCurrentSlotBundle.GetCurrentSlot());

				texturePair.rect = Rect(
					mCurrentSlotBundle.GetCurrentSlot().rect.x,
					mCurrentSlotBundle.GetCurrentSlot().rect.y,
					mCurrentSlotBundle.GetCurrentSlot().rect.w * requiredHorizontalSlots,
					mCurrentSlotBundle.GetCurrentSlot().rect.h * requiredVerticalSlots
				);

				RemoveSlotFromAvailableSlots(mCurrentSlotBundle.GetCurrentSlot().id);

				for (auto& validNeighbouringSlot : mCurrentSlotBundle.GetNeighbourSlots())// validNeighbouringSlots)
				{
					validNeighbouringSlot.Occupy();
					//AddToFilledSlots(validNeighbouringSlot);
					RemoveSlotFromAvailableSlots(validNeighbouringSlot.id);
				}

				//TS_CORE_INFO("Found slots for texture with name {0}, width {1}, height {2}",
				//	Texture2D::GetTextureFromID(texturePair.texID)->GetPath(),
				//	Texture2D::GetTextureFromID(texturePair.texID)->GetWidth(),
				//	Texture2D::GetTextureFromID(texturePair.texID)->GetHeight());

				texturePair.checkedPlacement = true;
				texturePair.placed = true;
				return;
			}
		}
	}

}

void TextureAtlas::UncheckAllSlots()
{
	for (auto slot : mAvailableSlots)
		slot.Uncheck();
}

void TextureAtlas::AddTextureToTextureStPairs(TextureStPair textureStPair)
{
	mTextureStPairs.push_back(textureStPair);
}

Slot* TextureAtlas::GetSlot(uint32_t x, uint32_t y)
{
	uint32_t index = x + y * mNumHorizontalSlots;

	for (auto& slot : mAvailableSlots)
	{
		if (slot.id == index)
			return &slot;
	}

	return nullptr;
}

Slot* TextureAtlas::GetNextVacantSlotIndex()
{
	for (auto& slot : mAvailableSlots)
	{
		if (!slot.checkedSlot)
			return &slot;
	}

	return nullptr;
}

void TextureAtlas::RemoveSlotFromAvailableSlots(uint32_t slotIndex)
{
	for (int i = 0; i < mAvailableSlots.size(); i++)
	{
		if (mAvailableSlots[i].id == slotIndex)
		{
			mAvailableSlots.erase(mAvailableSlots.begin() + i);
			//TS_CORE_INFO("Removed slot with index {0} from availableSlots", slotIndex);
		}
	}
}

const AtlasSizeAndTextureRectPair TextureAtlas::GetAtlasSizeAndTextureRectPair(uint32_t texID) const
{
	auto it = mSlotBundleMap.find(texID);

	if (it != mSlotBundleMap.end())
	{
		SlotBundle slotBundle = it->second;
		return AtlasSizeAndTextureRectPair(MATH::Vector2f(mAtlasWidth, mAtlasHeight), slotBundle.GetRect());
	}
	else
	{
		//TS_CORE_ERROR("Could not find texture ID: {0}", texID);
	}

	ASSERT("TextureAtlas::GetAtlasSizeAndTextureRectPair bad texID");
	return AtlasSizeAndTextureRectPair(MATH::Vector2f(mAtlasWidth, mAtlasHeight), {});
}

void TextureAtlas::CreateTextureAtlasTexture()
{
	mAtlasTexture = std::make_shared<RENDER::TextureGl>();
	GLuint texID = CreateTextureForAtlas(mSlotBundles, mAtlasWidth, mAtlasHeight);
	mAtlasTexture->id = texID;
}
class Writer {
	std::ofstream ofs;
public:
	Writer(const char* filename) : ofs(filename, std::ios_base::out | std::ios_base::binary) {}
	void operator () (const void* pbuf, int size) { ofs.write(static_cast<const char*>(pbuf), size); }
	template <typename T> void operator () (const T& obj) { operator () (&obj, sizeof(obj)); }
};
struct BIH {
	unsigned int   sz;
	int            width, height;
	unsigned short planes;
	short          bits;
	unsigned int   compress, szimage;
	int            xppm, yppm;
	unsigned int   clrused, clrimp;
};
void TextureAtlas::AddTextureToAtlas(Rect rect, int channels,
	std::vector<unsigned char> texData, std::vector<GLubyte>& atlasData)
{
	if (channels == 3)
	{
		for (int row = 0; row < rect.h; row++)
		{
			for (int col = 0; col < rect.w; col++)
			{
				int srcIndex = (row * rect.w + col) * channels;
				int dstIndex = ((rect.y + row) * mAtlasWidth + (rect.x + col)) * 4;

				atlasData[dstIndex + 0] = texData[srcIndex + 0];
				atlasData[dstIndex + 1] = texData[srcIndex + 1];
				atlasData[dstIndex + 2] = texData[srcIndex + 2];
				atlasData[dstIndex + 3] = 255;//Set alpha as 255
			}
		}
	}
	else//4 Channel
	{
		for (int row = 0; row < rect.h; row++)
		{
			for (int col = 0; col < rect.w; col++)
			{
				int srcIndex = (row * rect.w + col) * channels;
				int dstIndex = ((rect.y + row) * mAtlasWidth + (rect.x + col)) * channels;

				atlasData[dstIndex + 0] = texData[srcIndex + 0];
				atlasData[dstIndex + 1] = texData[srcIndex + 1];
				atlasData[dstIndex + 2] = texData[srcIndex + 2];
				atlasData[dstIndex + 3] = texData[srcIndex + 3];
			}
		}
	}
}

GLuint TextureAtlas::CreateTextureForAtlas(std::vector<SlotBundle> slotBundles, float atlasWidth, float atlasHeight)
{
	GLuint atlasTexture;
	glGenTextures(1, &atlasTexture);
	glBindTexture(GL_TEXTURE_2D, atlasTexture);

	// Set texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Define texture data
	std::vector<GLubyte> atlasData(atlasWidth * atlasHeight * 4, 255);

	//TODO: fix it
	//for (uint32_t i = 0; i < slotBundles.size(); i++)
	//{
	//	std::vector<unsigned char> texData = RENDER::TextureGl::GetPixels(UTILS::GetRealPath(slotBundles[i].GetTexture()->getPath()));
	//	uint32_t channels = slotBundles[i].GetTexture()->getChannels();
	//	Rect rect = slotBundles[i].GetRect();
	//
	//	AddTextureToAtlas(rect, channels, texData, atlasData);
	//}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasData.data());

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//glGenerateMipmap(GL_TEXTURE_2D);
	static int i = 0;
	auto fileName = "out" + std::to_string(i) + ".bmp";
	i++;
	Writer w(fileName.c_str());;
	w("BM", 2);
	BIH bih = { sizeof(bih) };
	bih.width = atlasWidth;
	bih.height = -atlasHeight;
	bih.planes = 1;
	bih.bits = 32;
	const unsigned int headersize = sizeof(bih) + 14;
	const int szbuf = atlasData.size();
	const unsigned int filesize = static_cast<unsigned int>(headersize + szbuf);
	w(filesize);
	const unsigned short z = 0;
	w(z);
	w(z);
	w(headersize);
	w(bih);
	w(atlasData.data(), szbuf);



	return atlasTexture;
}
//------------------------------------
class TextureAtlasCreator
{
public:
	TextureAtlasCreator(float atlasWidth, float atlasHeight, std::vector<std::shared_ptr<RENDER::TextureGl>> textures);
	~TextureAtlasCreator();

	void StartAtlasCreation();

	std::vector<TexturePair> GetTexturePairs();
	const std::vector<TextureAtlas> GetTextureAtlases() const
	{
		return mAtlases;
	}
private:
	float mAtlasWidth;
	float mAtlasHeight;
	std::vector<std::shared_ptr<RENDER::TextureGl>> mTextures;
	std::vector<TexturePair> mTexturePairs;
	std::vector<TextureAtlas> mAtlases;
	void CreateAtlas();
};

TextureAtlasCreator::TextureAtlasCreator(float atlasWidth, float atlasHeight, std::vector<std::shared_ptr<RENDER::TextureGl>> textures)
{
	mAtlasWidth = atlasWidth;
	mAtlasHeight = atlasHeight;
	mTextures = textures;

	StartAtlasCreation();
}

TextureAtlasCreator::~TextureAtlasCreator()
{

}

void TextureAtlasCreator::StartAtlasCreation()
{
	if (mTextures.size() == 0)
	{
		//TS_CORE_ERROR("No texture assigned for atlasing!");
		return;
	}

	for (auto& texture : mTextures)
	{
		TexturePair texturePair = TexturePair();
		texturePair.texID = texture;
		//texturePair.texture = texture;
		texturePair.placed = false;
		texturePair.checkedPlacement = false;

		mTexturePairs.push_back(texturePair);
	}

	CreateAtlas();
}

std::vector<TexturePair> TextureAtlasCreator::GetTexturePairs()
{
	return mTexturePairs;
}

void TextureAtlasCreator::CreateAtlas()
{
	TextureAtlas atlas = TextureAtlas(mAtlasWidth, mAtlasHeight, mTexturePairs);

	std::vector<TexturePair> unplacedTexturePairList = {};

	for (auto& texturePair : mTexturePairs)
	{
		texturePair.checkedPlacement = false;
		texturePair.placed = false;
		atlas.OccupySlotsForTexture(texturePair);
		atlas.UncheckAllSlots();

		if (!texturePair.placed)
			unplacedTexturePairList.push_back(texturePair);
		else
		{
			TextureStPair textureStPair = TextureStPair(texturePair.texID, texturePair.rect);
			atlas.AddTextureToTextureStPairs(textureStPair);
		}
	}

	atlas.CreateTextureAtlasTexture();
	mAtlases.push_back(atlas);
}
//--------------------------

std::map<std::string, std::shared_ptr<TextureAtlasCreator>> mTextureAtlasCreator;

void BatchComponent::CreateAtlases(const MaterialRenderer& material)
{
	std::map<std::string, std::vector<std::shared_ptr<RENDER::TextureGl>>> dataForAtlas;

	const auto& ms = material.getMaterials();
	for (const auto& m : ms) {
		if (!m) break;
		auto _m = std::static_pointer_cast<RENDER::MaterialGl>(m);
		//for (const auto& uniform : _m->mUniform) {
		//	if (std::holds_alternative<std::shared_ptr<RENDER::TextureGl>>(uniform.second)) {
		//		dataForAtlas[uniform.first].push_back(std::get<std::shared_ptr<RENDER::TextureGl>>(uniform.second));
		//	}
		//}
	}


	for (auto& atlasData : dataForAtlas) {
		if (atlasData.second.size() > 0 && !atlasData.second[0]) {
			continue;
		}
		mTextureAtlasCreator[atlasData.first] = std::make_shared<TextureAtlasCreator>(2048.0f, 2048.0f, atlasData.second);

		ids.push_back(mTextureAtlasCreator[atlasData.first]->GetTextureAtlases()[0].mAtlasTexture->id);
	}

	//for (auto& textureAtlas : mTextureAtlasCreator->GetTextureAtlases()) {
	//	auto atlasTex = textureAtlas.GetTexture();
	//	mAtlasTextures.push_back(atlasTex);
	//}
}

void BatchComponent::createBuffers(ModelRenderer& model, MaterialRenderer& material) {
	uint32_t lastTotalVertices = 0;
	std::vector<std::vector<Vertex>> _globalVerticesPerMesh;
	std::vector< std::vector<uint32_t>> _globalIndicesPerMesh;
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	auto path = fs.getFilePath(model.getModel()->getPath());
	if (!path) {
		//problem
	}
	RESOURCES::ModelLoader::CreateVerts(*path, RESOURCES::ModelLoader::getDefaultFlag(), _globalVerticesPerMesh, _globalIndicesPerMesh);

	std::vector<Vertex> mBatchedVertices;
	std::vector<uint32_t> mBatchedIndices;
	//for (auto& go : gameObjects) {
		uint32_t meshIndex = 0;

		//uint32_t texID = go->GetTextureID();
		//TS_CORE_INFO("Texture ID for {0} is {1}",go->GetName(), texID);


		//for (int i = 0; i < _globalVerticesPerMesh.size(); ++i) {
		//	auto m = material.getMaterials().size() > i ? material.getMaterials()[i] : material.getMaterials()[0];
		//	auto _m = std::static_pointer_cast<RENDER::MaterialGl>(m);
		//	auto tex = std::get<std::shared_ptr<RENDER::TextureGl>>(_m->mUniformData[mTextureAtlasCreator.begin()->first]);
		//
		//	AtlasSizeAndTextureRectPair atlasAndTextureRectPair = mTextureAtlasCreator.begin()->second->GetTextureAtlases().begin()->GetAtlasSizeAndTextureRectPair(tex->id);
		//
		//	Rect rect = atlasAndTextureRectPair.GetRect();//Rect between 0 - texWidth, 0, texHeight
		//	float atlasWidth = atlasAndTextureRectPair.GetAtlasSize().x;
		//	float atlasHeight = atlasAndTextureRectPair.GetAtlasSize().y;
		//
		//	float u1 = rect.x;
		//	float v1 = rect.y;
		//	float u2 = (rect.x + rect.w);
		//	float v2 = (rect.y + rect.h);
		//	//--------------------------------
		//
		//	auto& vertices = _globalVerticesPerMesh[i];
		//	auto& indices = _globalIndicesPerMesh[i];
		//
		//	for (const auto& vertex : vertices) {
		//		auto batchedVertex = vertex;
		//
		//		float u = (u1 + vertex.texCoord.x * rect.w) / atlasWidth;
		//		float v = (v1 + vertex.texCoord.y * rect.h) / atlasHeight;
		//
		//		//TS_CORE_INFO("Texcoord original : {0}, {1}", vertex.texCoord.x, vertex.texCoord.y);
		//		//TS_CORE_INFO("Texcoord : {0}, {1}", u, v);
		//		
		//		batchedVertex.texCoord = {u,v};
		//
		//		//TS_CORE_INFO("UV: {0}, {1}", vertex.uv.x, vertex.uv.y);
		//		//batchedVertex.texID = currentMeshTexID;
		//		mBatchedVertices.push_back(batchedVertex);
		//	}
		//
		//	for (const uint32_t& index : indices)
		//		mBatchedIndices.push_back(lastTotalVertices + index);
		//
		//	lastTotalVertices += vertices.size();
		//
		//	//TS_CORE_INFO("Texture ID for {0}'s mesh at {1} is {2}",go->GetName(), meshIndex ,go->GetTextureID());
		//	//auto tex2D = Texture2D::GetTextureFromID(go->GetTextureID());
		//
		//	meshIndex++;
		////}
		//}

	auto newMash = std::make_shared<RENDER::MeshGl>(mBatchedVertices, mBatchedIndices, 0);
	auto newModel = std::make_shared<RENDER::ModelGl>("batchModel");
	newModel->setMeshes({ newMash });
	newModel->setMaterialNames({ "default" });
	model.setModel(newModel);

	auto _m = std::static_pointer_cast<RENDER::MaterialGl>(material.getMaterials()[0]);

	//TODO: make copy method
	auto newMaterial = std::make_shared<RENDER::MaterialGl>();
	//newMaterial->mUniforms = _m->mUniforms;
	//newMaterial->mUniformData = _m->mUniformData;
	//newMaterial->mShader = _m->mShader;
	//TODO: fix it
	//newMaterial->mBlendable = _m->mBlendable;
	//newMaterial->mBackfaceCulling = _m->mBackfaceCulling;
	//newMaterial->mFrontfaceCulling = _m->mFrontfaceCulling;
	//newMaterial->mDepthTest = _m->mDepthTest;
	//newMaterial->mDepthWriting = _m->mDepthWriting;
	//newMaterial->mColorWriting = _m->mColorWriting;
	//newMaterial->mGpuInstances = _m->mGpuInstances;
	//newMaterial->mIsDeferred = _m->mIsDeferred;
	//newMaterial->mIsCastShadow = _m->mIsCastShadow;
	//newMaterial->mIsBakedShadow = _m->mIsBakedShadow;

	//for (auto& tex : mTextureAtlasCreator) {
	//	newMaterial->mUniformData[tex.first] = tex.second->GetTextureAtlases()[0].mAtlasTexture;
	//}

	material.fillWithMaterial(newMaterial);
}


//#include <rttr/registration>
//
//RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::BatchComponent>("BatchComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
//}
#endif

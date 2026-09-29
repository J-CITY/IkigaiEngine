#include "bone.h"
#include "bone.h"

#include <cassert>
#include <unordered_set>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <glm/fwd.hpp>

#include "../parser/assimpParser.h"
#include "utilsModule/pathGetter.h"

using namespace IKIGAI;
using namespace IKIGAI::RESOURCES;
#define MAX_BONES 100

Bone::Bone(const std::string& name, int ID, const aiNodeAnim* channel):
	m_Name(name),
	m_ID(ID),
	m_LocalTransform(1.0f) {
	m_NumPositions = channel->mNumPositionKeys;

	for (int positionIndex = 0; positionIndex < m_NumPositions; ++positionIndex) {
		aiVector3D aiPosition = channel->mPositionKeys[positionIndex].mValue;
		float timeStamp = channel->mPositionKeys[positionIndex].mTime;
		KeyPosition data;
		data.position = ConvertVector3(aiPosition);
		data.timeStamp = timeStamp;
		m_Positions.push_back(data);
	}

	m_NumRotations = channel->mNumRotationKeys;
	for (int rotationIndex = 0; rotationIndex < m_NumRotations; ++rotationIndex) {
		aiQuaternion aiOrientation = channel->mRotationKeys[rotationIndex].mValue;
		float timeStamp = channel->mRotationKeys[rotationIndex].mTime;
		KeyRotation data;
		data.orientation = ConvertQuaternion(aiOrientation);
		data.timeStamp = timeStamp;
		m_Rotations.push_back(data);
	}

	m_NumScalings = channel->mNumScalingKeys;
	for (int keyIndex = 0; keyIndex < m_NumScalings; ++keyIndex) {
		aiVector3D scale = channel->mScalingKeys[keyIndex].mValue;
		float timeStamp = channel->mScalingKeys[keyIndex].mTime;
		KeyScale data;
		data.scale = ConvertVector3(scale);
		data.timeStamp = timeStamp;
		m_Scales.push_back(data);
	}
}

void Bone::Update(float animationTime) {
	auto translation = InterpolatePosition(animationTime);
	auto rotation = InterpolateRotation(animationTime);
	auto scale = InterpolateScaling(animationTime);
	m_LocalTransform = translation * rotation * scale;
}

std::string Bone::GetBoneName() const
{ return m_Name; }

int Bone::GetBoneID()
{ return m_ID; }

int Bone::GetPositionIndex(float animationTime) {
	for (int index = 0; index < m_NumPositions - 1; ++index) {
		if (animationTime < m_Positions[index + 1].timeStamp)
			return index;
	}
	//assert(0);
	return 0;
}

int Bone::GetRotationIndex(float animationTime) {
	for (int index = 0; index < m_NumRotations - 1; ++index) {
		if (animationTime < m_Rotations[index + 1].timeStamp)
			return index;
	}
	//assert(0);
	return 0;
}

int Bone::GetScaleIndex(float animationTime) {
	for (int index = 0; index < m_NumScalings - 1; ++index) {
		if (animationTime < m_Scales[index + 1].timeStamp)
			return index;
	}
	//assert(0);
	return 0;
}

float Bone::GetScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime) {
	float scaleFactor = 0.0f;
	float midWayLength = animationTime - lastTimeStamp;
	float framesDiff = nextTimeStamp - lastTimeStamp;
	scaleFactor = midWayLength / framesDiff;
	return scaleFactor;
}

MATH::Matrix4f Bone::InterpolatePosition(float animationTime) {
    if (1 == m_NumPositions) {
        return MATH::Matrix4f::Translation(m_Positions[0].position);
    }
    int p0Index = GetPositionIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Positions[p0Index].timeStamp,
        m_Positions[p1Index].timeStamp, animationTime);
    auto finalPosition = MATH::Vector3f::Mix(m_Positions[p0Index].position, m_Positions[p1Index].position, scaleFactor);
	return MATH::Matrix4f::Translation(finalPosition);
}

MATH::Matrix4f Bone::InterpolateRotation(float animationTime) {
    if (1 == m_NumRotations) {
        return MATH::QuaternionF::ToMatrix4(MATH::QuaternionF::Normalize(m_Rotations[0].orientation));
    }

    int p0Index = GetRotationIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Rotations[p0Index].timeStamp, m_Rotations[p1Index].timeStamp, animationTime);
    auto res = MATH::QuaternionF::Slerp(m_Rotations[p0Index].orientation, m_Rotations[p1Index].orientation, scaleFactor);
    return MATH::QuaternionF::ToMatrix4(MATH::QuaternionF::Normalize(res));
}

MATH::Matrix4f Bone::InterpolateScaling(float animationTime) {
    if (1 == m_NumScalings)
        return MATH::Matrix4f::Scaling(m_Scales[0].scale);

    int p0Index = GetScaleIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Scales[p0Index].timeStamp, m_Scales[p1Index].timeStamp, animationTime);
    auto finalScale = MATH::Vector3f::Mix(m_Scales[p0Index].scale, m_Scales[p1Index].scale, scaleFactor);
    return MATH::Matrix4f::Scaling(finalScale);
}

Animation::Animation() = default;

Animation::~Animation() = default;

Bone* Animation::FindBone(const std::string& name) {
	auto iter = std::ranges::find_if(m_Bones.begin(), m_Bones.end(), [&](const Bone& Bone) {
		return Bone.GetBoneName() == name;
	});
	if (iter == m_Bones.end()) return nullptr;
	else return &(*iter);
}

float Animation::GetTicksPerSecond()
{ return m_TicksPerSecond; }

float Animation::GetDuration()
{ return m_Duration; }

const AssimpNodeData& Animation::GetRootNode()
{ return m_RootNode; }

const std::map<std::string, RENDER::BoneInfo>& Animation::GetBoneIDMap() {
	return m_BoneInfoMap;
}

void Animation::ReadMissingBones(const aiAnimation& animation, RENDER::ModelInterface& model) {
	int size = animation.mNumChannels;

	auto& boneInfoMap = model.GetBoneInfoMap();//getting m_BoneInfoMap from Model class
	int& boneCount = model.GetBoneCount(); //getting the m_BoneCounter from Model class

	//reading channels(bones engaged in an animation and their keyframes)
	for (int i = 0; i < size; i++) {
		auto channel = animation.mChannels[i];
		std::string boneName = channel->mNodeName.data;

		if (boneInfoMap.find(boneName) == boneInfoMap.end()) {
			boneInfoMap[boneName].mId = boneCount;
			boneCount++;
		}
		m_Bones.push_back(Bone(channel->mNodeName.data,
		                       boneInfoMap[channel->mNodeName.data].mId, channel));
	}

	m_BoneInfoMap = boneInfoMap;
}

void Animation::ReadHeirarchyData(AssimpNodeData& dest, const aiNode* src) {
	assert(src);

	dest.name = src->mName.data;
	dest.transformation = ConvertMatrix4x4(src->mTransformation);
	dest.childrenCount = src->mNumChildren;

	for (int i = 0; i < src->mNumChildren; i++) {
		AssimpNodeData newData;
		ReadHeirarchyData(newData, src->mChildren[i]);
		dest.children.push_back(newData);
	}
}

Animation::Animation(const std::string& animationPath, RENDER::ModelInterface* model) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);
    assert(scene && scene->mRootNode);
    auto animation = scene->mAnimations[0];
    m_Duration = animation->mDuration;
    m_TicksPerSecond = animation->mTicksPerSecond;
    aiMatrix4x4 globalTransformation = scene->mRootNode->mTransformation;
    globalTransformation = globalTransformation.Inverse();
    ReadHeirarchyData(m_RootNode, scene->mRootNode);
    ReadMissingBones(*animation, *model);
    //TODO: remode it
    this->model = model;
}


Animation::Animation(const aiAnimation& animation, const aiScene& scene, RENDER::ModelInterface& model) {
    m_Duration = animation.mDuration;
    m_TicksPerSecond = animation.mTicksPerSecond;
    aiMatrix4x4 globalTransformation = scene.mRootNode->mTransformation;
    globalTransformation = globalTransformation.Inverse();
    ReadHeirarchyData(m_RootNode, scene.mRootNode);
    ReadMissingBones(animation, model);
    this->model = &model;
}

std::vector<MATH::Matrix4f> Animator::GetFinalBoneMatrices() {
	return m_FinalBoneMatrices;
}

std::map<std::string, std::shared_ptr<Animation>> Animation::LoadAnimations(const std::string& animationPath, RENDER::ModelInterface* model) {
    std::map<std::string, std::shared_ptr<Animation>> res;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);
    assert(scene && scene->mRootNode);

    for (auto i = 0u; i < scene->mNumAnimations; i++) {
        auto animation = scene->mAnimations[i];
        auto name = animation->mName;
        res[name.C_Str()] = std::shared_ptr<Animation>(new Animation (*animation, *scene, *model));
    }

    return res;
}

Animator::Animator(Animation* animation) {
    m_CurrentTime = 0.0;
    m_CurrentAnimation = animation;

    m_FinalBoneMatrices.reserve(100);

    for (int i = 0; i < 100; i++)
        m_FinalBoneMatrices.push_back(MATH::Matrix4f(1.0f));

    {//TODO:: remove it
        blender = new Blander();
        blender->m_Animations["idle"] = new Animation("models/player/animations/Idle.dae", m_CurrentAnimation->model);
        blender->m_Animations["run_forward"] = new Animation("models/player/animations/RunForward.dae", m_CurrentAnimation->model);
        blender->m_Animations["run_backward"] = new Animation("models/player/animations/RunBackward.dae", m_CurrentAnimation->model);
        blender->m_Animations["run_left"] = new Animation("models/player/animations/RunLeft.dae", m_CurrentAnimation->model);
        blender->m_Animations["run_right"] = new Animation("models/player/animations/RunRight.dae", m_CurrentAnimation->model);

        std::vector<MATH::Vector2f> points;

        points.push_back({-0.1, -0.1});
        points.push_back({1.1, -0.1});
        points.push_back({1.1, 1.1});
        points.push_back({-0.1, 1.1});

        points.push_back({0.0, 0.5}); //idle
        points.push_back({1.0, 0.5}); //f
        points.push_back({1.0, 0.0}); //b
        points.push_back({1.0, 1.0}); //b
        points.push_back({1.0, 0.75}); //l
        points.push_back({1.0, 0.25}); //r

        std::vector<std::string> animNames = {"idle", "idle", "idle", "idle", 
			"idle", "run_forward", "run_backward", "run_backward","run_left" , "run_right"};
        blender->setPoints(points, animNames);
    }
}

void Animator::PlayAnimation(Animation* pAnimation) {
    m_CurrentAnimation = pAnimation;
    m_CurrentTime = 0.0f;
}

void Animator::CalculateBoneTransform(const AssimpNodeData* node, MATH::Matrix4f parentTransform) {
    std::string nodeName = node->name;
    auto nodeTransform = node->transformation;

    Bone* bone = m_CurrentAnimation->FindBone(nodeName);

    if (bone) {
        bone->Update(m_CurrentTime);
        nodeTransform = bone->GetLocalTransform();
    }

    auto globalTransformation = parentTransform * nodeTransform;

    auto boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
    if (boneInfoMap.find(nodeName) != boneInfoMap.end()) {
        int index = boneInfoMap[nodeName].mId;
        auto offset = boneInfoMap[nodeName].mOffset;
        m_FinalBoneMatrices[index] = globalTransformation * offset;
    }

    for (int i = 0; i < node->childrenCount; i++) {
        CalculateBoneTransform(&node->children[i], globalTransformation);
    }
}

void Animator::BlendTwoAnimations(Animation* pBaseAnimation, Animation* pLayeredAnimation, float blendFactor, float deltaTime) {
    // Speed multipliers to correctly transition from one animation to another
    float a = 1.0f;
    float b = pBaseAnimation->GetDuration() / pLayeredAnimation->GetDuration();
    const float animSpeedMultiplierUp = (1.0f - blendFactor) * a + b * blendFactor; // Lerp

    a = pLayeredAnimation->GetDuration() / pBaseAnimation->GetDuration();
    b = 1.0f;
    const float animSpeedMultiplierDown = (1.0f - blendFactor) * a + b * blendFactor; // Lerp

    // Current time of each animation, "scaled" by the above speed multiplier variables
    static float currentTimeBase = 0.0f;
    currentTimeBase += pBaseAnimation->GetTicksPerSecond() * deltaTime * animSpeedMultiplierUp;
    currentTimeBase = fmod(currentTimeBase, pBaseAnimation->GetDuration());

    static float currentTimeLayered = 0.0f;
    currentTimeLayered += pLayeredAnimation->GetTicksPerSecond() * deltaTime * animSpeedMultiplierDown;
    currentTimeLayered = fmod(currentTimeLayered, pLayeredAnimation->GetDuration());

    CalculateBlendTwoBoneTransform(pBaseAnimation, &pBaseAnimation->GetRootNode(), pLayeredAnimation, &pLayeredAnimation->GetRootNode(), currentTimeBase, currentTimeLayered, MATH::Matrix4f(1.0f), blendFactor);
}


// Recursive function that sets interpolated bone matrices in the 'm_FinalBoneMatrices' vector
void Animator::CalculateBlendTwoBoneTransform(
    Animation* pAnimationBase, const AssimpNodeData* node,
    Animation* pAnimationLayer, const AssimpNodeData* nodeLayered,
    const float currentTimeBase, const float currentTimeLayered,
    const MATH::Matrix4f& parentTransform,
    const float blendFactor) {
    const std::string& nodeName = node->name;

    MATH::Matrix4f nodeTransform = node->transformation;
    Bone* pBone = pAnimationBase->FindBone(nodeName);
    if (pBone) {
        pBone->Update(currentTimeBase);
        nodeTransform = pBone->GetLocalTransform();
    }

    MATH::Matrix4f layeredNodeTransform = nodeLayered->transformation;
    pBone = pAnimationLayer->FindBone(nodeName);
    if (pBone) {
        pBone->Update(currentTimeLayered);
        layeredNodeTransform = pBone->GetLocalTransform();
    }

    //// Blend two matrices
    //const auto rot0 = MATH::QuaternionF(nodeTransform);
    //const auto rot1 = MATH::QuaternionF(layeredNodeTransform);
    //const auto finalRot = MATH::QuaternionF::Slerp(rot0, rot1, blendFactor);
    //MATH::Matrix4f blendedMat = MATH::QuaternionF::ToMatrix4(finalRot);
    ////mix position
    //blendedMat.getData()[3] = (1.0f - blendFactor) * nodeTransform.getData()[3] + layeredNodeTransform.getData()[3] * blendFactor;
    //blendedMat.getData()[7] = (1.0f - blendFactor) * nodeTransform.getData()[7] + layeredNodeTransform.getData()[7] * blendFactor;
    //blendedMat.getData()[11] = (1.0f - blendFactor) * nodeTransform.getData()[11] + layeredNodeTransform.getData()[11] * blendFactor;

    MATH::Matrix4f blendedMat = nodeTransform * (1.0f - blendFactor) + layeredNodeTransform * blendFactor;

    auto globalTransformation = parentTransform * blendedMat;

    const auto& boneInfoMap = pAnimationBase->GetBoneIDMap();
    if (boneInfoMap.find(nodeName) != boneInfoMap.end()) {
        const int index = boneInfoMap.at(nodeName).mId;
        const auto& offset = boneInfoMap.at(nodeName).mOffset;
        if (index < 100) {
            m_FinalBoneMatrices[index] = globalTransformation * offset;
        }
    }

    for (size_t i = 0; i < node->children.size(); ++i) {
        CalculateBlendTwoBoneTransform(pAnimationBase, &node->children[i], pAnimationLayer, &nodeLayered->children[i], currentTimeBase, currentTimeLayered, globalTransformation, blendFactor);
    }
}


//blenmd 3

void Animator::BlendThreeAnimations(Animation* pBaseAnimation, Animation* pLayeredAnimation1, Animation* pLayeredAnimation2, float blendFactor1, float blendFactor2, float blendFactor3, float deltaTime) {
    // Speed multipliers to correctly transition from one animation to another
    float a = 1.0f;
    float b = pBaseAnimation->GetDuration() / pLayeredAnimation1->GetDuration();
    float c = pLayeredAnimation2->GetDuration() / pLayeredAnimation1->GetDuration();
    const float animSpeedMultiplierUp = (blendFactor1) * a + (blendFactor2) * b + (blendFactor3) * c; // Lerp

    a = pLayeredAnimation1->GetDuration() / pBaseAnimation->GetDuration();
    b = 1.0f;
    c = pLayeredAnimation2->GetDuration() / pBaseAnimation->GetDuration();
    const float animSpeedMultiplierDown1 = (blendFactor1) * a + (blendFactor2) * b + (blendFactor3) * c; // Lerp

    a = pLayeredAnimation1->GetDuration() / pLayeredAnimation2->GetDuration();
    b = pBaseAnimation->GetDuration() / pLayeredAnimation2->GetDuration();
    c = 1.0f;
    const float animSpeedMultiplierDown2 = (blendFactor1) * a + (blendFactor2) * b + (blendFactor3) * c; // Lerp

    // Current time of each animation, "scaled" by the above speed multiplier variables
    static float currentTimeBase = 0.0f;
    currentTimeBase += pBaseAnimation->GetTicksPerSecond() * deltaTime * animSpeedMultiplierUp;
    currentTimeBase = fmod(currentTimeBase, pBaseAnimation->GetDuration());

    static float currentTimeLayered1 = 0.0f;
    currentTimeLayered1 += pLayeredAnimation1->GetTicksPerSecond() * deltaTime * animSpeedMultiplierDown1;
    currentTimeLayered1 = fmod(currentTimeLayered1, pLayeredAnimation1->GetDuration());

    static float currentTimeLayered2 = 0.0f;
    currentTimeLayered2 += pLayeredAnimation2->GetTicksPerSecond() * deltaTime * animSpeedMultiplierDown2;
    currentTimeLayered2 = fmod(currentTimeLayered2, pLayeredAnimation2->GetDuration());

    CalculateBlendThreeBoneTransform(
        pBaseAnimation, &pBaseAnimation->GetRootNode(), 
        pLayeredAnimation1, &pLayeredAnimation1->GetRootNode(), 
        pLayeredAnimation2, &pLayeredAnimation2->GetRootNode(), 
        currentTimeBase, currentTimeLayered1, currentTimeLayered2, 
        MATH::Matrix4f(1.0f), 
        blendFactor1, blendFactor2, blendFactor3);
}


// Recursive function that sets interpolated bone matrices in the 'm_FinalBoneMatrices' vector
void Animator::CalculateBlendThreeBoneTransform(
    Animation* pAnimationBase, const AssimpNodeData* node,
    Animation* pAnimationLayer1, const AssimpNodeData* nodeLayered1,
    Animation* pAnimationLayer2, const AssimpNodeData* nodeLayered2,
    const float currentTimeBase, const float currentTimeLayered1, const float currentTimeLayered2,
    const MATH::Matrix4f& parentTransform,
    const float blendFactor1, const float blendFactor2, const float blendFactor3
) {
    const std::string& nodeName = node->name;

    MATH::Matrix4f nodeTransform = node->transformation;
    Bone* pBone = pAnimationBase->FindBone(nodeName);
    if (pBone) {
        pBone->Update(currentTimeBase);
        nodeTransform = pBone->GetLocalTransform();
    }

    MATH::Matrix4f layeredNodeTransform1 = nodeLayered1->transformation;
    pBone = pAnimationLayer1->FindBone(nodeName);
    if (pBone) {
        pBone->Update(currentTimeLayered1);
        layeredNodeTransform1 = pBone->GetLocalTransform();
    }

    MATH::Matrix4f layeredNodeTransform2 = nodeLayered2->transformation;
    pBone = pAnimationLayer2->FindBone(nodeName);
    if (pBone) {
        pBone->Update(currentTimeLayered2);
        layeredNodeTransform2 = pBone->GetLocalTransform();
    }

    // Blend 3 matrices
    //const auto rot0 = MATH::QuaternionF(nodeTransform);
    //const auto rot1 = MATH::QuaternionF(layeredNodeTransform1);
    //const auto rot2 = MATH::QuaternionF(layeredNodeTransform2);
    //auto finalRot = MATH::QuaternionF::Slerp(rot0, rot1, blendFactor1);
    //finalRot = MATH::QuaternionF::Slerp(rot2, finalRot, blendFactor3);
    //MATH::Matrix4f blendedMat = MATH::QuaternionF::ToMatrix4(finalRot.Normalize(finalRot));
    MATH::Matrix4f blendedMat = blendFactor1 * nodeTransform + layeredNodeTransform1 * blendFactor2 + layeredNodeTransform2 * blendFactor3;
    //blendedMat.getData()[0] = blendFactor1 * nodeTransform.getData()[0] + layeredNodeTransform1.getData()[0] * blendFactor2 + layeredNodeTransform2.getData()[0] * blendFactor3;
    //blendedMat.getData()[5] = blendFactor1 * nodeTransform.getData()[5] + layeredNodeTransform1.getData()[5] * blendFactor2 + layeredNodeTransform2.getData()[5] * blendFactor3;
    //blendedMat.getData()[10] = blendFactor1 * nodeTransform.getData()[10] + layeredNodeTransform1.getData()[10] * blendFactor2 + layeredNodeTransform2.getData()[10] * blendFactor3;
    ////mix position
    //blendedMat.getData()[3] = blendFactor1 * nodeTransform.getData()[3] + layeredNodeTransform1.getData()[3] * blendFactor2 + layeredNodeTransform2.getData()[3] * blendFactor3;
    //blendedMat.getData()[7] = blendFactor1 * nodeTransform.getData()[7] + layeredNodeTransform1.getData()[7] * blendFactor2 + layeredNodeTransform2.getData()[7] * blendFactor3;
    //blendedMat.getData()[11] = blendFactor1 * nodeTransform.getData()[11] + layeredNodeTransform1.getData()[11] * blendFactor2 + layeredNodeTransform2.getData()[11] * blendFactor3;

    auto globalTransformation = parentTransform * blendedMat;

    const auto& boneInfoMap = pAnimationBase->GetBoneIDMap();
    if (boneInfoMap.find(nodeName) != boneInfoMap.end()) {
        const int index = boneInfoMap.at(nodeName).mId;
        const auto& offset = boneInfoMap.at(nodeName).mOffset;
        if (index < 100)
	        m_FinalBoneMatrices[index] = globalTransformation * offset;
    }

    for (size_t i = 0; i < node->children.size(); ++i) {
        CalculateBlendThreeBoneTransform(
            pAnimationBase, &node->children[i], 
            pAnimationLayer1, &nodeLayered1->children[i],
            pAnimationLayer2, &nodeLayered2->children[i], 
            currentTimeBase, currentTimeLayered1, currentTimeLayered2, 
            globalTransformation, 
            blendFactor1, blendFactor2, blendFactor3);
    }
}

namespace geometry {

    //@see https://stackoverflow.com/questions/33333363/built-in-mod-vs-custom-mod-function-improve-the-performance-of-modulus-op/33333636#33333636
    inline size_t fast_mod(const size_t i, const size_t c) {
        return i >= c ? i % c : i;
    }

    // Kahan and Babuska summation, Neumaier variant; accumulates less FP error
    inline double sum(const std::vector<double>& x) {
        double sum = x[0];
        double err = 0.0;

        for (size_t i = 1; i < x.size(); i++) {
            const double k = x[i];
            const double m = sum + k;
            err += std::fabs(sum) >= std::fabs(k) ? sum - m + k : k - m + sum;
            sum = m;
        }
        return sum + err;
    }

    inline double dist(
        const double ax,
        const double ay,
        const double bx,
        const double by) {
        const double dx = ax - bx;
        const double dy = ay - by;
        return dx * dx + dy * dy;
    }

    inline double circumradius(
        const double ax,
        const double ay,
        const double bx,
        const double by,
        const double cx,
        const double cy) {
        const double dx = bx - ax;
        const double dy = by - ay;
        const double ex = cx - ax;
        const double ey = cy - ay;

        const double bl = dx * dx + dy * dy;
        const double cl = ex * ex + ey * ey;
        const double d = dx * ey - dy * ex;

        const double x = (ey * bl - dy * cl) * 0.5 / d;
        const double y = (dx * cl - ex * bl) * 0.5 / d;

        if ((bl > 0.0 || bl < 0.0) && (cl > 0.0 || cl < 0.0) && (d > 0.0 || d < 0.0)) {
            return x * x + y * y;
        } else {
            return std::numeric_limits<double>::max();
        }
    }

    inline bool orient(
        const double px,
        const double py,
        const double qx,
        const double qy,
        const double rx,
        const double ry) {
        return (qy - py) * (rx - qx) - (qx - px) * (ry - qy) < 0.0;
    }

    inline std::pair<double, double> circumcenter(
        const double ax,
        const double ay,
        const double bx,
        const double by,
        const double cx,
        const double cy) {
        const double dx = bx - ax;
        const double dy = by - ay;
        const double ex = cx - ax;
        const double ey = cy - ay;

        const double bl = dx * dx + dy * dy;
        const double cl = ex * ex + ey * ey;
        const double d = dx * ey - dy * ex;

        const double x = ax + (ey * bl - dy * cl) * 0.5 / d;
        const double y = ay + (dx * cl - ex * bl) * 0.5 / d;

        return std::make_pair(x, y);
    }

    struct compare {

        std::vector<double> const& coords;
        double cx;
        double cy;

        bool operator()(std::size_t i, std::size_t j) {
            const double d1 = dist(coords[2 * i], coords[2 * i + 1], cx, cy);
            const double d2 = dist(coords[2 * j], coords[2 * j + 1], cx, cy);
            const double diff1 = d1 - d2;
            const double diff2 = coords[2 * i] - coords[2 * j];
            const double diff3 = coords[2 * i + 1] - coords[2 * j + 1];

            if (diff1 > 0.0 || diff1 < 0.0) {
                return diff1 < 0;
            } else if (diff2 > 0.0 || diff2 < 0.0) {
                return diff2 < 0;
            } else {
                return diff3 < 0;
            }
        }
    };

    inline bool in_circle(
        const double ax,
        const double ay,
        const double bx,
        const double by,
        const double cx,
        const double cy,
        const double px,
        const double py) {
        const double dx = ax - px;
        const double dy = ay - py;
        const double ex = bx - px;
        const double ey = by - py;
        const double fx = cx - px;
        const double fy = cy - py;

        const double ap = dx * dx + dy * dy;
        const double bp = ex * ex + ey * ey;
        const double cp = fx * fx + fy * fy;

        return (dx * (ey * cp - bp * fy) -
            dy * (ex * cp - bp * fx) +
            ap * (ex * fy - ey * fx)) < 0.0;
    }

    constexpr double EPSILON = std::numeric_limits<double>::epsilon();
    constexpr std::size_t INVALID_INDEX = std::numeric_limits<std::size_t>::max();

    inline bool check_pts_equal(double x1, double y1, double x2, double y2) {
        return std::fabs(x1 - x2) <= EPSILON &&
            std::fabs(y1 - y2) <= EPSILON;
    }

    // monotonically increases with real angle, but doesn't need expensive trigonometry
    inline double pseudo_angle(const double dx, const double dy) {
        const double p = dx / (std::abs(dx) + std::abs(dy));
        return (dy > 0.0 ? 3.0 - p : 1.0 + p) / 4.0; // [0..1)
    }

    struct DelaunatorPoint {
        std::size_t i;
        double x;
        double y;
        std::size_t t;
        std::size_t prev;
        std::size_t next;
        bool removed;
    };

    class Delaunator {

    public:
        std::vector<double> const coords;
        std::vector<std::size_t> triangles;
        std::vector<std::size_t> halfedges;
        std::vector<std::size_t> hull_prev;
        std::vector<std::size_t> hull_next;
        std::vector<std::size_t> hull_tri;
        std::size_t hull_start;

        Delaunator(std::vector<double> const& in_coords);

        double get_hull_area();

    private:
        std::vector<std::size_t> m_hash;
        double m_center_x;
        double m_center_y;
        std::size_t m_hash_size;
        std::vector<std::size_t> m_edge_stack;

        std::size_t legalize(std::size_t a);
        std::size_t hash_key(double x, double y) const;
        std::size_t add_triangle(
            std::size_t i0,
            std::size_t i1,
            std::size_t i2,
            std::size_t a,
            std::size_t b,
            std::size_t c);
        void link(std::size_t a, std::size_t b);
    };

    Delaunator::Delaunator(std::vector<double> const& in_coords)
        : coords(in_coords),
        triangles(),
        halfedges(),
        hull_prev(),
        hull_next(),
        hull_tri(),
        hull_start(),
        m_hash(),
        m_center_x(),
        m_center_y(),
        m_hash_size(),
        m_edge_stack() {
        std::size_t n = coords.size() >> 1;

        double max_x = std::numeric_limits<double>::min();
        double max_y = std::numeric_limits<double>::min();
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        std::vector<std::size_t> ids;
        ids.reserve(n);

        for (std::size_t i = 0; i < n; i++) {
            const double x = coords[2 * i];
            const double y = coords[2 * i + 1];

            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;

            ids.push_back(i);
        }
        const double cx = (min_x + max_x) / 2;
        const double cy = (min_y + max_y) / 2;
        double min_dist = std::numeric_limits<double>::max();

        std::size_t i0 = INVALID_INDEX;
        std::size_t i1 = INVALID_INDEX;
        std::size_t i2 = INVALID_INDEX;

        // pick a seed point close to the centroid
        for (std::size_t i = 0; i < n; i++) {
            const double d = dist(cx, cy, coords[2 * i], coords[2 * i + 1]);
            if (d < min_dist) {
                i0 = i;
                min_dist = d;
            }
        }

        const double i0x = coords[2 * i0];
        const double i0y = coords[2 * i0 + 1];

        min_dist = std::numeric_limits<double>::max();

        // find the point closest to the seed
        for (std::size_t i = 0; i < n; i++) {
            if (i == i0) continue;
            const double d = dist(i0x, i0y, coords[2 * i], coords[2 * i + 1]);
            if (d < min_dist && d > 0.0) {
                i1 = i;
                min_dist = d;
            }
        }

        double i1x = coords[2 * i1];
        double i1y = coords[2 * i1 + 1];

        double min_radius = std::numeric_limits<double>::max();

        // find the third point which forms the smallest circumcircle with the first two
        for (std::size_t i = 0; i < n; i++) {
            if (i == i0 || i == i1) continue;

            const double r = circumradius(
                i0x, i0y, i1x, i1y, coords[2 * i], coords[2 * i + 1]);

            if (r < min_radius) {
                i2 = i;
                min_radius = r;
            }
        }

        if (!(min_radius < std::numeric_limits<double>::max())) {
            throw std::runtime_error("not triangulation");
        }

        double i2x = coords[2 * i2];
        double i2y = coords[2 * i2 + 1];

        if (orient(i0x, i0y, i1x, i1y, i2x, i2y)) {
            std::swap(i1, i2);
            std::swap(i1x, i2x);
            std::swap(i1y, i2y);
        }

        std::tie(m_center_x, m_center_y) = circumcenter(i0x, i0y, i1x, i1y, i2x, i2y);

        // sort the points by distance from the seed triangle circumcenter
        std::sort(ids.begin(), ids.end(), compare{coords, m_center_x, m_center_y});

        // initialize a hash table for storing edges of the advancing convex hull
        m_hash_size = static_cast<std::size_t>(std::llround(std::ceil(std::sqrt(n))));
        m_hash.resize(m_hash_size);
        std::fill(m_hash.begin(), m_hash.end(), INVALID_INDEX);

        // initialize arrays for tracking the edges of the advancing convex hull
        hull_prev.resize(n);
        hull_next.resize(n);
        hull_tri.resize(n);

        hull_start = i0;

        size_t hull_size = 3;

        hull_next[i0] = hull_prev[i2] = i1;
        hull_next[i1] = hull_prev[i0] = i2;
        hull_next[i2] = hull_prev[i1] = i0;

        hull_tri[i0] = 0;
        hull_tri[i1] = 1;
        hull_tri[i2] = 2;

        m_hash[hash_key(i0x, i0y)] = i0;
        m_hash[hash_key(i1x, i1y)] = i1;
        m_hash[hash_key(i2x, i2y)] = i2;

        std::size_t max_triangles = n < 3 ? 1 : 2 * n - 5;
        triangles.reserve(max_triangles * 3);
        halfedges.reserve(max_triangles * 3);
        add_triangle(i0, i1, i2, INVALID_INDEX, INVALID_INDEX, INVALID_INDEX);
        double xp = std::numeric_limits<double>::quiet_NaN();
        double yp = std::numeric_limits<double>::quiet_NaN();
        for (std::size_t k = 0; k < n; k++) {
            const std::size_t i = ids[k];
            const double x = coords[2 * i];
            const double y = coords[2 * i + 1];

            // skip near-duplicate points
            if (k > 0 && check_pts_equal(x, y, xp, yp)) continue;
            xp = x;
            yp = y;

            // skip seed triangle points
            if (
                check_pts_equal(x, y, i0x, i0y) ||
                check_pts_equal(x, y, i1x, i1y) ||
                check_pts_equal(x, y, i2x, i2y)) continue;

            // find a visible edge on the convex hull using edge hash
            std::size_t start = 0;

            size_t key = hash_key(x, y);
            for (size_t j = 0; j < m_hash_size; j++) {
                start = m_hash[fast_mod(key + j, m_hash_size)];
                if (start != INVALID_INDEX && start != hull_next[start]) break;
            }

            start = hull_prev[start];
            size_t e = start;
            size_t q;

            while (q = hull_next[e], !orient(x, y, coords[2 * e], coords[2 * e + 1], coords[2 * q], coords[2 * q + 1])) { //TODO: does it works in a same way as in JS
                e = q;
                if (e == start) {
                    e = INVALID_INDEX;
                    break;
                }
            }

            if (e == INVALID_INDEX) continue; // likely a near-duplicate point; skip it

            // add the first triangle from the point
            std::size_t t = add_triangle(
                e,
                i,
                hull_next[e],
                INVALID_INDEX,
                INVALID_INDEX,
                hull_tri[e]);

            hull_tri[i] = legalize(t + 2);
            hull_tri[e] = t;
            hull_size++;

            // walk forward through the hull, adding more triangles and flipping recursively
            std::size_t next = hull_next[e];
            while (
                q = hull_next[next],
                orient(x, y, coords[2 * next], coords[2 * next + 1], coords[2 * q], coords[2 * q + 1])) {
                t = add_triangle(next, i, q, hull_tri[i], INVALID_INDEX, hull_tri[next]);
                hull_tri[i] = legalize(t + 2);
                hull_next[next] = next; // mark as removed
                hull_size--;
                next = q;
            }

            // walk backward from the other side, adding more triangles and flipping
            if (e == start) {
                while (
                    q = hull_prev[e],
                    orient(x, y, coords[2 * q], coords[2 * q + 1], coords[2 * e], coords[2 * e + 1])) {
                    t = add_triangle(q, i, e, INVALID_INDEX, hull_tri[e], hull_tri[q]);
                    legalize(t + 2);
                    hull_tri[q] = t;
                    hull_next[e] = e; // mark as removed
                    hull_size--;
                    e = q;
                }
            }

            // update the hull indices
            hull_prev[i] = e;
            hull_start = e;
            hull_prev[next] = i;
            hull_next[e] = i;
            hull_next[i] = next;

            m_hash[hash_key(x, y)] = i;
            m_hash[hash_key(coords[2 * e], coords[2 * e + 1])] = e;
        }
    }

    double Delaunator::get_hull_area() {
        std::vector<double> hull_area;
        size_t e = hull_start;
        do {
            hull_area.push_back((coords[2 * e] - coords[2 * hull_prev[e]]) * (coords[2 * e + 1] + coords[2 * hull_prev[e] + 1]));
            e = hull_next[e];
        } while (e != hull_start);
        return sum(hull_area);
    }

    std::size_t Delaunator::legalize(std::size_t a) {
        std::size_t i = 0;
        std::size_t ar = 0;
        m_edge_stack.clear();

        // recursion eliminated with a fixed-size stack
        while (true) {
            const size_t b = halfedges[a];

            /* if the pair of triangles doesn't satisfy the Delaunay condition
            * (p1 is inside the circumcircle of [p0, pl, pr]), flip them,
            * then do the same check/flip recursively for the new pair of triangles
            *
            *           pl                    pl
            *          /||\                  /  \
            *       al/ || \bl            al/    \a
            *        /  ||  \              /      \
            *       /  a||b  \    flip    /___ar___\
            *     p0\   ||   /p1   =>   p0\---bl---/p1
            *        \  ||  /              \      /
            *       ar\ || /br             b\    /br
            *          \||/                  \  /
            *           pr                    pr
            */
            const size_t a0 = 3 * (a / 3);
            ar = a0 + (a + 2) % 3;

            if (b == INVALID_INDEX) {
                if (i > 0) {
                    i--;
                    a = m_edge_stack[i];
                    continue;
                } else {
                    //i = INVALID_INDEX;
                    break;
                }
            }

            const size_t b0 = 3 * (b / 3);
            const size_t al = a0 + (a + 1) % 3;
            const size_t bl = b0 + (b + 2) % 3;

            const std::size_t p0 = triangles[ar];
            const std::size_t pr = triangles[a];
            const std::size_t pl = triangles[al];
            const std::size_t p1 = triangles[bl];

            const bool illegal = in_circle(
                coords[2 * p0],
                coords[2 * p0 + 1],
                coords[2 * pr],
                coords[2 * pr + 1],
                coords[2 * pl],
                coords[2 * pl + 1],
                coords[2 * p1],
                coords[2 * p1 + 1]);

            if (illegal) {
                triangles[a] = p1;
                triangles[b] = p0;

                auto hbl = halfedges[bl];

                // edge swapped on the other side of the hull (rare); fix the halfedge reference
                if (hbl == INVALID_INDEX) {
                    std::size_t e = hull_start;
                    do {
                        if (hull_tri[e] == bl) {
                            hull_tri[e] = a;
                            break;
                        }
                        e = hull_next[e];
                    } while (e != hull_start);
                }
                link(a, hbl);
                link(b, halfedges[ar]);
                link(ar, bl);
                std::size_t br = b0 + (b + 1) % 3;

                if (i < m_edge_stack.size()) {
                    m_edge_stack[i] = br;
                } else {
                    m_edge_stack.push_back(br);
                }
                i++;

            } else {
                if (i > 0) {
                    i--;
                    a = m_edge_stack[i];
                    continue;
                } else {
                    break;
                }
            }
        }
        return ar;
    }

    inline std::size_t Delaunator::hash_key(const double x, const double y) const {
        const double dx = x - m_center_x;
        const double dy = y - m_center_y;
        return fast_mod(
            static_cast<std::size_t>(std::llround(std::floor(pseudo_angle(dx, dy) * static_cast<double>(m_hash_size)))),
            m_hash_size);
    }

    std::size_t Delaunator::add_triangle(
        std::size_t i0,
        std::size_t i1,
        std::size_t i2,
        std::size_t a,
        std::size_t b,
        std::size_t c) {
        std::size_t t = triangles.size();
        triangles.push_back(i0);
        triangles.push_back(i1);
        triangles.push_back(i2);
        link(t, a);
        link(t + 1, b);
        link(t + 2, c);
        return t;
    }

    void Delaunator::link(const std::size_t a, const std::size_t b) {
        std::size_t s = halfedges.size();
        if (a == s) {
            halfedges.push_back(b);
        } else if (a < s) {
            halfedges[a] = b;
        } else {
            throw std::runtime_error("Cannot link edge");
        }
        if (b != INVALID_INDEX) {
            std::size_t s2 = halfedges.size();
            if (b == s2) {
                halfedges.push_back(a);
            } else if (b < s2) {
                halfedges[b] = a;
            } else {
                throw std::runtime_error("Cannot link edge");
            }
        }
    }
}  // namespace geometry

namespace IKIGAI
{
    class BlenderContext {
    public:
        BlenderContext() {

        }

        std::unique_ptr<geometry::Delaunator> triangulation;
        std::map<std::pair<float, float>, std::string> animNames;
    };
}


Blander::Blander() {
    context = std::make_unique<IKIGAI::BlenderContext>();
}
float scaleFactor = 1.0f;
void Blander::setPoints(const std::vector<MATH::Vector2f>& points, const std::vector<std::string>& animNames) {
    std::vector<double> _points;
    int i = 0;
    for (auto& e : points) {
        _points.emplace_back(e.x * scaleFactor);
        _points.emplace_back(e.y * scaleFactor);
        context->animNames[std::make_pair(e.x * scaleFactor, e.y * scaleFactor)] = animNames[i];
        ++i;
    }

    context->triangulation = std::make_unique<geometry::Delaunator>(_points);
}

/* A utility function to calculate area of triangle formed by (x1, y1),
   (x2, y2) and (x3, y3) */
float area(float x1, float y1, float x2, float y2, float x3, float y3) {
    return std::abs((x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2)) / 2.0f);
}

/* A function to check whether point P(x, y) lies inside the triangle formed
   by A(x1, y1), B(x2, y2) and C(x3, y3) */
bool isInside(float x1, float y1, float x2, float y2, float x3, float y3, float x, float y) {
    /* Calculate area of triangle ABC */
    float A = area(x1, y1, x2, y2, x3, y3);

    /* Calculate area of triangle PBC */
    float A1 = area(x, y, x2, y2, x3, y3);

    /* Calculate area of triangle PAC */
    float A2 = area(x1, y1, x, y, x3, y3);

    /* Calculate area of triangle PAB */
    float A3 = area(x1, y1, x2, y2, x, y);

    const auto eps = 1e-9;
    /* Check if sum of A1, A2 and A3 is same as A */
    return (A - (A1 + A2 + A3)) <= eps;
}

float calculateDistance(float ax, float ay, float bx, float by) {
    return std::sqrt(std::pow(ax - bx, 2) + std::pow(ay - by, 2));
}

std::vector<Blander::AnimWithBlendInfo> Blander::getAnimationForBlending(MATH::Vector2f point) {
    std::vector<Blander::AnimWithBlendInfo> res;
    point.x *= scaleFactor;
    point.y *= scaleFactor;
    for (std::size_t i = 0; i < context->triangulation->triangles.size(); i += 3) {
        float ax = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i]]);        //tx0
        float ay = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i] + 1]);    //ty0
        float bx = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i + 1]]);    //tx1
        float by = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i + 1] + 1]);//ty1
        float cx = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i + 2]]);    //tx2
        float cy = static_cast<float>(context->triangulation->coords[2 * context->triangulation->triangles[i + 2] + 1]); //ty2
	    if (isInside(ax, ay, bx, by, cx, cy, point.x, point.y)) {

            float dist0 = 0.0f;
            if (ax >= hor.x && ax <= hor.y) {
                dist0 = calculateDistance(ax, ay, point.x, point.y);
            }

            float dist1 = 0.0f;
            if (bx >= hor.x && bx <= hor.y) {
                dist1 = calculateDistance(bx, by, point.x, point.y);
            }

            float dist2 = 0.0f;
            if (cx >= hor.x && cx <= hor.y) {
                dist2 = calculateDistance(cx, cy, point.x, point.y);
            }

            auto n = dist0 + dist1 + dist2;

            dist0 /= n;
            dist1 /= n;
            dist2 /= n;

            if (dist0 > dist1 && dist0 > dist2) {
                if (dist1 < dist2) {
                    std::swap(dist0, dist1);
                }
                else {
                    std::swap(dist0, dist2);
                }
            }
            else if (dist1 > dist0 && dist1 > dist2) {
                if (dist0 < dist2) {
                    std::swap(dist1, dist0);
                } else {
                    std::swap(dist1, dist2);
                }
            }
            else if (dist2 > dist1 && dist2 > dist0) {
                if (dist0 < dist1) {
                    std::swap(dist2, dist0);
                } else {
                    std::swap(dist2, dist1);
                }
            }
 
            res.push_back({context->animNames[{ax, ay}], static_cast<float>(dist0)});
            res.push_back({context->animNames[{bx, by}], static_cast<float>(dist1)});
            res.push_back({context->animNames[{cx, cy}], static_cast<float>(dist2)});

            //TODO: sort

            break;
	    }
    }

    return res;
}


void Animator::UpdateAnimation(float dt) {
    //if (!m_CurrentAnimation) {
    //    return;
    //}
    if (!blender) {
        return;
    }
    m_DeltaTime = dt;
    //if (m_CurrentAnimation) {
    //	m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * m_DeltaTime;
    //    m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
    //    CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), MATH::Matrix4f(1.0f));
    //}
    if (blender) {
        auto res = blender->getAnimationForBlending(blender->point);
        //auto anim = blender->m_Animations[res[0].name];
        //m_CurrentTime += anim->GetTicksPerSecond() * m_DeltaTime;
        //m_CurrentTime = fmod(m_CurrentTime, anim->GetDuration());
        //CalculateBoneTransform(&anim->GetRootNode(), MATH::Matrix4f(1.0f));
        BlendTwoAnimations(
            blender->m_Animations[res[0].name], blender->m_Animations[res[1].name],
            res[0].factor, m_DeltaTime);
    	//BlendThreeAnimations(
        //    blender->m_Animations[res[0].name], blender->m_Animations[res[1].name], blender->m_Animations[res[2].name],
        //    res[0].factor, res[1].factor, res[2].factor, m_DeltaTime);
    }
}
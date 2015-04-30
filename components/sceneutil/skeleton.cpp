#include "skeleton.hpp"

#include <osg/Transform>
#include <osg/MatrixTransform>

#include <iostream>

#include "bone.hpp"

namespace SceneUtil
{

class InitBoneCacheVisitor : public osg::NodeVisitor
{
public:
    InitBoneCacheVisitor(std::map<std::string, std::pair<osg::NodePath, osg::MatrixTransform*> >& cache)
        : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        , mCache(cache)
    {
    }

    void apply(osg::Transform &node)
    {
        osg::MatrixTransform* bone = node.asMatrixTransform();
        if (!bone)
            return;

        mCache[bone->getName()] = std::make_pair(getNodePath(), bone);

        traverse(node);
    }
private:
    std::map<std::string, std::pair<osg::NodePath, osg::MatrixTransform*> >& mCache;
};

Skeleton::Skeleton()
    : mBoneCacheInit(false)
    , mNeedToUpdateBoneMatrices(true)
    , mLastFrameNumber(0)
    , mActive(true)
{

}

Skeleton::Skeleton(const Skeleton &copy, const osg::CopyOp &copyop)
    : osg::Group(copy, copyop)
    , mBoneCacheInit(false)
    , mNeedToUpdateBoneMatrices(true)
    , mLastFrameNumber(0)
    , mActive(copy.mActive)
{

}

Bone* Skeleton::getBone(const std::string &name)
{
    if (!mBoneCacheInit)
    {
        InitBoneCacheVisitor visitor(mBoneCache);
        accept(visitor);
        mBoneCacheInit = true;
    }

    BoneCache::iterator found = mBoneCache.find(name);
    if (found == mBoneCache.end())
        return NULL;

    // find or insert in the bone hierarchy

    if (!mRootBone.get())
    {
        mRootBone.reset(new Bone);
    }

    const osg::NodePath& path = found->second.first;
    Bone* bone = mRootBone.get();
    for (osg::NodePath::const_iterator it = path.begin(); it != path.end(); ++it)
    {
        osg::MatrixTransform* matrixTransform = dynamic_cast<osg::MatrixTransform*>(*it);
        if (!matrixTransform)
            continue;

        Bone* child = NULL;
        for (unsigned int i=0; i<bone->mChildren.size(); ++i)
        {
            if (bone->mChildren[i]->mNode == *it)
            {
                child = bone->mChildren[i];
                break;
            }
        }

        if (!child)
        {
            child = new Bone;
            bone->mChildren.push_back(child);
            mNeedToUpdateBoneMatrices = true;
        }
        bone = child;

        bone->mNode = matrixTransform;
    }

    return bone;
}

void Skeleton::updateBoneMatrices(osg::NodeVisitor* nv)
{
    if (nv->getFrameStamp()->getFrameNumber() != mLastFrameNumber)
        mNeedToUpdateBoneMatrices = true;

    mLastFrameNumber = nv->getFrameStamp()->getFrameNumber();

    if (mNeedToUpdateBoneMatrices)
    {
        if (mRootBone.get())
        {
            for (unsigned int i=0; i<mRootBone->mChildren.size(); ++i)
                mRootBone->mChildren[i]->update(NULL);
        }
        else
            std::cerr << "no root bone" << std::endl;

        mNeedToUpdateBoneMatrices = false;
    }
}

void Skeleton::setActive(bool active)
{
    mActive = active;
}

bool Skeleton::getActive() const
{
    return mActive;
}

}

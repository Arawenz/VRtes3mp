#ifndef OPENMW_MWPHYSICS_CONTACTTESTRESULTCALLBACK_H
#define OPENMW_MWPHYSICS_CONTACTTESTRESULTCALLBACK_H

#include <vector>

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include "physicssystem.hpp"

class btCollisionObject;
struct btCollisionObjectWrapper;

namespace MWPhysics
{
    class ContactTestResultCallback : public btCollisionWorld::ContactResultCallback
    {
        const btCollisionObject* mTestedAgainst;

    public:
        ContactTestResultCallback(const btCollisionObject* testedAgainst);

        btScalar addSingleResult(btManifoldPoint& cp,
                                         const btCollisionObjectWrapper* col0Wrap,int partId0,int index0,
                                         const btCollisionObjectWrapper* col1Wrap,int partId1,int index1) override;

        std::vector<ContactPoint> mResult;
    };
}

#endif

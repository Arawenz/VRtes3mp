#include "tilecachedrecastmeshmanager.hpp"
#include "makenavmesh.hpp"
#include "gettilespositions.hpp"
#include "settingsutils.hpp"

#include <algorithm>
#include <vector>

namespace DetourNavigator
{
    TileCachedRecastMeshManager::TileCachedRecastMeshManager(const Settings& settings)
        : mSettings(settings)
    {}

    bool TileCachedRecastMeshManager::addObject(const ObjectId id, const CollisionShape& shape,
                                                const btTransform& transform, const AreaType areaType)
    {
        std::vector<TilePosition> tilesPositions;
        const auto border = getBorderSize(mSettings);
        {
            auto tiles = mTiles.lock();
            getTilesPositions(shape.getShape(), transform, mSettings, [&] (const TilePosition& tilePosition)
                {
                    if (addTile(id, shape, transform, areaType, tilePosition, border, tiles.get()))
                        tilesPositions.push_back(tilePosition);
                });
        }
        if (tilesPositions.empty())
            return false;
        std::sort(tilesPositions.begin(), tilesPositions.end());
        mObjectsTilesPositions.insert_or_assign(id, std::move(tilesPositions));
        ++mRevision;
        return true;
    }

    std::optional<RemovedRecastMeshObject> TileCachedRecastMeshManager::removeObject(const ObjectId id)
    {
        const auto object = mObjectsTilesPositions.find(id);
        if (object == mObjectsTilesPositions.end())
            return std::nullopt;
        std::optional<RemovedRecastMeshObject> result;
        {
            auto tiles = mTiles.lock();
            for (const auto& tilePosition : object->second)
            {
                const auto removed = removeTile(id, tilePosition, tiles.get());
                if (removed && !result)
                    result = removed;
            }
        }
        if (result)
            ++mRevision;
        return result;
    }

    bool TileCachedRecastMeshManager::addWater(const osg::Vec2i& cellPosition, const int cellSize,
        const btTransform& transform)
    {
        const auto border = getBorderSize(mSettings);

        auto& tilesPositions = mWaterTilesPositions[cellPosition];

        bool result = false;

        if (cellSize == std::numeric_limits<int>::max())
        {
            const auto tiles = mTiles.lock();
            for (auto& tile : *tiles)
            {
                if (tile.second->addWater(cellPosition, cellSize, transform))
                {
                    tilesPositions.push_back(tile.first);
                    result = true;
                }
            }
        }
        else
        {
            getTilesPositions(cellSize, transform, mSettings, [&] (const TilePosition& tilePosition)
                {
                    const auto tiles = mTiles.lock();
                    auto tile = tiles->find(tilePosition);
                    if (tile == tiles->end())
                    {
                        auto tileBounds = makeTileBounds(mSettings, tilePosition);
                        tileBounds.mMin -= osg::Vec2f(border, border);
                        tileBounds.mMax += osg::Vec2f(border, border);
                        tile = tiles->insert(std::make_pair(tilePosition,
                                std::make_shared<CachedRecastMeshManager>(mSettings, tileBounds, mTilesGeneration))).first;
                    }
                    if (tile->second->addWater(cellPosition, cellSize, transform))
                    {
                        tilesPositions.push_back(tilePosition);
                        result = true;
                    }
                });
        }

        if (result)
            ++mRevision;

        return result;
    }

    std::optional<RecastMeshManager::Water> TileCachedRecastMeshManager::removeWater(const osg::Vec2i& cellPosition)
    {
        const auto object = mWaterTilesPositions.find(cellPosition);
        if (object == mWaterTilesPositions.end())
            return std::nullopt;
        std::optional<RecastMeshManager::Water> result;
        for (const auto& tilePosition : object->second)
        {
            const auto tiles = mTiles.lock();
            const auto tile = tiles->find(tilePosition);
            if (tile == tiles->end())
                continue;
            const auto tileResult = tile->second->removeWater(cellPosition);
            if (tile->second->isEmpty())
            {
                tiles->erase(tile);
                ++mTilesGeneration;
            }
            if (tileResult && !result)
                result = tileResult;
        }
        if (result)
            ++mRevision;
        return result;
    }

    std::shared_ptr<RecastMesh> TileCachedRecastMeshManager::getMesh(const TilePosition& tilePosition)
    {
        const auto manager = [&] () -> std::shared_ptr<CachedRecastMeshManager>
        {
            const auto tiles = mTiles.lock();
            const auto it = tiles->find(tilePosition);
            if (it == tiles->end())
                return nullptr;
            return it->second;
        } ();
        if (manager == nullptr)
            return nullptr;
        return manager->getMesh();
    }

    bool TileCachedRecastMeshManager::hasTile(const TilePosition& tilePosition)
    {
        return mTiles.lockConst()->count(tilePosition);
    }

    std::size_t TileCachedRecastMeshManager::getRevision() const
    {
        return mRevision;
    }

    void TileCachedRecastMeshManager::reportNavMeshChange(const TilePosition& tilePosition, Version recastMeshVersion, Version navMeshVersion)
    {
        const auto tiles = mTiles.lock();
        const auto it = tiles->find(tilePosition);
        if (it == tiles->end())
            return;
        it->second->reportNavMeshChange(recastMeshVersion, navMeshVersion);
    }

    bool TileCachedRecastMeshManager::addTile(const ObjectId id, const CollisionShape& shape,
        const btTransform& transform, const AreaType areaType, const TilePosition& tilePosition, float border,
        TilesMap& tiles)
    {
        auto tile = tiles.find(tilePosition);
        if (tile == tiles.end())
        {
            auto tileBounds = makeTileBounds(mSettings, tilePosition);
            tileBounds.mMin -= osg::Vec2f(border, border);
            tileBounds.mMax += osg::Vec2f(border, border);
            tile = tiles.insert(std::make_pair(
                tilePosition, std::make_shared<CachedRecastMeshManager>(mSettings, tileBounds, mTilesGeneration))).first;
        }
        return tile->second->addObject(id, shape, transform, areaType);
    }

    bool TileCachedRecastMeshManager::updateTile(const ObjectId id, const btTransform& transform,
        const AreaType areaType, const TilePosition& tilePosition, TilesMap& tiles)
    {
        const auto tile = tiles.find(tilePosition);
        return tile != tiles.end() && tile->second->updateObject(id, transform, areaType);
    }

    std::optional<RemovedRecastMeshObject> TileCachedRecastMeshManager::removeTile(const ObjectId id,
        const TilePosition& tilePosition, TilesMap& tiles)
    {
        const auto tile = tiles.find(tilePosition);
        if (tile == tiles.end())
            return std::optional<RemovedRecastMeshObject>();
        const auto tileResult = tile->second->removeObject(id);
        if (tile->second->isEmpty())
        {
            tiles.erase(tile);
            ++mTilesGeneration;
        }
        return tileResult;
    }
}

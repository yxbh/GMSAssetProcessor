#include "ImageAssetsProcessor.hpp"

#include "gif.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace ke
{

    AssetsProcessorSptr ImageAssetsProcessor::createProcessor(
            ProgressAnnouncer progress, const QString & spriteFolderPath, const QString & texpageFolderPath,
            const QString & textureFolderPath, const QString & outputFolderPath,
            const QRgb & backgroundFillColour, bool overrideAlpha0PixelsWithBgFillColour,
            bool generateGif, std::size_t gifFrameDelayInMs)
    {
        QDir spriteFolderPathChecker(spriteFolderPath);
        QDir texpageFolderPathChecker(texpageFolderPath);
        QDir textureFolderPathPathChecker(textureFolderPath);
        QDir outputFolder(outputFolderPath);

        if (spriteFolderPathChecker.exists() &&
            texpageFolderPathChecker.exists() &&
            textureFolderPathPathChecker.exists())
        {
            if (!outputFolder.exists() && !outputFolder.mkpath(outputFolder.absolutePath()))
            {
                progress("Failure creating output directory.");
                return std::make_shared<NullAssetsProcessor>(progress);
            }

            auto processor = std::make_shared<ImageAssetsProcessor>(progress);

            processor->spriteFolderPath = spriteFolderPath;
            processor->texpageFolderPath = texpageFolderPath;
            processor->textureFolderPath = textureFolderPath;
            processor->processingOutputFolderPath = outputFolderPath;

            processor->backgroundFillColour = backgroundFillColour;
            processor->isOverrideAlpha0PixelsWithBgFillColour = overrideAlpha0PixelsWithBgFillColour;

            processor->isGeneratingGif = generateGif;
            processor->gifFrameDelayInMs = gifFrameDelayInMs;

            return processor;
        }

        return std::make_shared<NullAssetsProcessor>(progress);
    }

    bool ImageAssetsProcessor::process()
    {
        processSpriteFiles();
        processTexpageFiles();
        processTextureFiles();

        if (!generateSpriteFiles()) return false;

        if (this->isGeneratingGif && !generateGifFiles()) return false;

        return true;
    }

    void ImageAssetsProcessor::processSpriteFiles()
    {
        QDir spriteDirPath(spriteFolderPath);
        auto spriteFolderContents = spriteDirPath.entryInfoList(QStringList() << "*.json", QDir::Files);
        for (auto fileInfo : spriteFolderContents)
        {
            updateProgress("Processing: " + fileInfo.absoluteFilePath());

            QFile jsonFile(fileInfo.absoluteFilePath());

            if (!jsonFile.open(QFile::ReadOnly | QFile::Text)) break;
            QTextStream ifs(&jsonFile);
            const auto jsonDoc = QJsonDocument::fromJson(ifs.readAll().toUtf8());

            const auto jsonObjRoot = jsonDoc.object();
            const auto jsonObjSize = jsonObjRoot["size"].toObject();
            const auto jsonObjBbox = jsonObjRoot["bounding"].toObject();
            const auto jsonValBboxmode = jsonObjRoot["bboxmode"];
            const auto jsonValSepmasks = jsonObjRoot["sepmasks"];
            const auto jsonObjOrigin = jsonObjRoot["origin"].toObject();
            const auto jsonArrTextures = jsonObjRoot["textures"].toArray();

            Sprite sprite;
            sprite.name                 = fileInfo.baseName();
            sprite.dimension.width      = jsonObjSize["width"].toInt();
            sprite.dimension.height     = jsonObjSize["height"].toInt();
            sprite.boundingBox.top      = jsonObjBbox["top"].toInt();
            sprite.boundingBox.left     = jsonObjBbox["left"].toInt();
            sprite.boundingBox.bottom   = jsonObjBbox["bottom"].toInt();
            sprite.boundingBox.right    = jsonObjBbox["right"].toInt();
            sprite.boundingBoxMode      = jsonValBboxmode.toInt();
            sprite.separateMasks        = jsonValSepmasks.toBool();
            sprite.origin.x             = jsonObjOrigin["x"].toInt();
            sprite.origin.y             = jsonObjOrigin["y"].toInt();
            for (auto jsonVal : jsonArrTextures) sprite.texpageIds.push_back(jsonVal.toInt());

            sprites.push_back(sprite);
        }
    }

    void ImageAssetsProcessor::processTexpageFiles()
    {
        QDir texpageDirPath(texpageFolderPath);
        auto texpageFolderContents = texpageDirPath.entryInfoList(QStringList() << "*.json", QDir::Files);
        for (auto fileInfo : texpageFolderContents)
        {
            updateProgress("Processing: " + fileInfo.absoluteFilePath());

            QFile jsonFile(fileInfo.absoluteFilePath());

            if (!jsonFile.open(QFile::ReadOnly | QFile::Text)) break;
            QTextStream ifs(&jsonFile);
            const auto jsonDoc = QJsonDocument::fromJson(ifs.readAll().toUtf8());

            const auto jsonObjRoot = jsonDoc.object();
            const auto jsonObjSrc  = jsonObjRoot["src"].toObject();
            const auto jsonObjDest = jsonObjRoot["dest"].toObject();
            const auto jsonObjSize = jsonObjRoot["size"].toObject();
            const auto jsonValSheetid = jsonObjRoot["sheetid"];

            Texpage texpage;
            texpage.id = fileInfo.baseName().toInt();
            texpage.sourcePosition.x            = jsonObjSrc["x"].toInt();
            texpage.sourcePosition.y            = jsonObjSrc["y"].toInt();
            texpage.sourceDimension.width       = jsonObjSrc["width"].toInt();
            texpage.sourceDimension.height      = jsonObjSrc["height"].toInt();
            texpage.destinationPosition.x       = jsonObjDest["x"].toInt();
            texpage.destinationPosition.y       = jsonObjDest["y"].toInt();
            texpage.destinationDimension.width  = jsonObjDest["width"].toInt();
            texpage.destinationDimension.height = jsonObjDest["height"].toInt();
            texpage.dimension.width             = jsonObjSize["width"].toInt();
            texpage.dimension.height            = jsonObjSize["height"].toInt();
            texpage.textureId = jsonValSheetid.toInt();

            texpages.push_back(texpage);
        }
    }

    void ImageAssetsProcessor::processTextureFiles()
    {
        QDir textureDirPath(textureFolderPath);
        auto textureFolderContents = textureDirPath.entryInfoList(QStringList() << "*.png", QDir::Files);
        for (auto fileInfo : textureFolderContents)
        {
            updateProgress("Processing: " + fileInfo.absoluteFilePath());

            Texture texture;
            texture.id = fileInfo.baseName().toInt();
            texture.texture = QImage(fileInfo.absoluteFilePath());

            textures.push_back(texture);
        }
    }

    bool ImageAssetsProcessor::generateSpriteFiles()
    {
        QDir outputDir(processingOutputFolderPath);
        for (const Sprite & sprite : sprites)
        {
            updateProgress("Generating sprite image(s) for " + sprite.name);

            if (!outputDir.mkdir(sprite.name) &&
                !QDir(outputDir.filePath(sprite.name)).exists())
            {
                updateProgress("Failure generating sprite image(s) for " + sprite.name);
                return false;
            }

            QDir spriteDir(outputDir.filePath(sprite.name));
            int counter = 0;
            for (TexpageId texpageId : sprite.texpageIds)
            {
                auto texpageIt = std::find_if(texpages.constBegin(), texpages.constEnd(), [texpageId](const Texpage & texpage){ return (texpage.id == texpageId); });
                if (texpageIt == texpages.constEnd())
                {
                    updateProgress("Failure looking for texpage with id " + QString::number(texpageId));
                    return false;
                }

                const Texpage & texpage(*texpageIt);

                auto textureIt = std::find_if(textures.constBegin(), textures.constEnd(), [&texpage](const Texture & texture){ return texture.id == texpage.textureId; });
                if (textureIt == textures.constEnd())
                {
                    updateProgress("Failure looking for texture with id " + QString::number(texpage.textureId));
                    return false;
                }

                const Texture & texture(*textureIt);

                QImage spriteImage(sprite.dimension.width, sprite.dimension.height, texture.texture.format());
                spriteImage.fill(this->backgroundFillColour);
                QPainter painter(&spriteImage);
                painter.drawImage(texpage.destinationPosition.x, texpage.destinationPosition.y,
                                  texture.texture,
                                  texpage.sourcePosition.x, texpage.sourcePosition.y,
                                  texpage.sourceDimension.width, texpage.sourceDimension.height);

                if (this->isOverrideAlpha0PixelsWithBgFillColour)
                {
                    for (int row = 0; row < spriteImage.height(); ++row)
                    {
                        QRgb * rowPixels = reinterpret_cast<QRgb*>(spriteImage.scanLine(row));
                        for (int pixel_i = 0; pixel_i < spriteImage.width(); ++pixel_i)
                        {
                            if (qAlpha(rowPixels[pixel_i]) == 0)
                            {
                                rowPixels[pixel_i] = this->backgroundFillColour;
                            }
                        }
                    }
                }

                auto targetPath = spriteDir.filePath(sprite.name + "_" + QString::number(counter++)+".png");
                bool result = spriteImage.save(targetPath);
                if (!result)
                {
                    updateProgress("Failure saving extracted sprite image at " + targetPath);
                    return false;
                }
            }
        }

        return true;
    }

    bool ImageAssetsProcessor::generateGifFiles()
    {
        QDir outputDir(processingOutputFolderPath);
        for (const Sprite & sprite : sprites)
        {
            // ignore if the sprite has only 1 texture.
            if (sprite.texpageIds.size() <= 1) continue;

            QDir spriteDir(outputDir.filePath(sprite.name));
            auto spriteFolderContents = spriteDir.entryInfoList(QStringList() << "*.png", QDir::Files);
            QCollator collator;
            collator.setNumericMode(true);
            std::sort(
                spriteFolderContents.begin(),
                spriteFolderContents.end(),
                [&collator](const QFileInfo & fileInfo1, const QFileInfo & fileInfo2)
                {
                    return collator.compare(fileInfo1.baseName(), fileInfo2.baseName()) < 0;
                });

            if (spriteFolderContents.size() <= 1) continue;

            auto gifFilePath = spriteDir.filePath(sprite.name + ".gif");
            progressAnnouncer("Generating GIF: " + gifFilePath);

            GifWriter gifWriter;
            GifBegin(&gifWriter, gifFilePath.toStdString().c_str(),
                     sprite.dimension.width, sprite.dimension.height, gifFrameDelayInMs, 8, true);

            for (QFileInfo & fileInfo : spriteFolderContents)
            {
                const QImage spriteFrame(fileInfo.absoluteFilePath());

                // convert color channel order for gif lib.
                auto pixelsBegin = reinterpret_cast<const uint32_t*>(spriteFrame.constBits());
                auto pixelsEnd = pixelsBegin + spriteFrame.width() * spriteFrame.height();
                std::vector<uint32_t> pixelsRGBA(pixelsBegin, pixelsEnd);
                for (uint32_t & pixel : pixelsRGBA)
                {
                    pixel = ((pixel) & 0xff000000) | ((pixel << 16) & 0x00ff0000) | ((pixel) & 0x0000ff00) | ((pixel >> 16) & 0x000000ff);
                }

                GifWriteFrame(&gifWriter, reinterpret_cast<const uint8_t*>(pixelsRGBA.data()),
                              sprite.dimension.width, sprite.dimension.height, gifFrameDelayInMs, 8, true);
            }

            GifEnd(&gifWriter);
        }

        return true;
    }

}


/*******************************************************************************
 * services/TunnelTransfer.h                                                   *
 *                                                                             *
 * Copyright (C) 2026 defnax          <retroshare.project@gmail.com>           *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Affero General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Affero General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Affero General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#pragma once

#include <QByteArray>
#include <QVariantMap>
#include <map>

// Frames stay well below RetroShare's 262143-byte serialized packet limit.
namespace GitTunnelTransfer {
constexpr int ChunkSize = 48 * 1024;
constexpr int MaxSize = 128 * 1024 * 1024;

struct Assembly {
    int size = 0;
    std::map<int, QByteArray> chunks;

    // 1: complete, 0: waiting, -1: invalid. Accept out-of-order delivery.
    int add(const QVariantMap& frame, QByteArray& result) {
        bool sizeOk = false, indexOk = false;
        int total = frame.value("size").toInt(&sizeOk);
        int index = frame.value("index").toInt(&indexOk);
        if (!sizeOk || !indexOk || total <= 0 || total > MaxSize ||
            index < 0 || index >= (total + ChunkSize - 1) / ChunkSize ||
            (size && size != total)) return -1;
        QByteArray encoded = frame.value("data").toByteArray();
        if (encoded.size() > 4 * ((ChunkSize + 2) / 3)) return -1;
        QByteArray bytes = QByteArray::fromBase64(encoded);
        if (bytes.toBase64() != encoded ||
            bytes.size() != qMin(ChunkSize, total - index * ChunkSize)) return -1;
        size = total;
        auto existing = chunks.find(index);
        if (existing != chunks.end() && existing->second != bytes) return -1;
        chunks[index] = bytes;
        if (int(chunks.size()) != (size + ChunkSize - 1) / ChunkSize) return 0;
        result.clear();
        result.reserve(size);
        for (const auto& chunk : chunks) result.append(chunk.second);
        return 1;
    }
};
}

//
// Created by Mengyu Li on 2018/8/10.
// Copyright (c) 2018 top.limengyu. All rights reserved.
//

import Foundation

public typealias Race = DataTask

public protocol DataTask {
    /// url
    var url: String { get }

    /// default nil , will merge agent's value
    var headers: [String: Encodable]? { get }

    /// default GET
    var method: Method { get }

    /// default .json
    var contentType: ContentType { get }

    /// default nil
    var parameters: [String: Any]? { get }

    /// default use gearbox's value
    var options: RaceOptions { get }

    /// default use gearbox's value
    var speedLimit: RaceSpeedLimit { get }

    /// default use gearbox's value
    var proxy: (String, Int)? { get }
}

extension Race {

    public var speedLimit: TaskSpeedLimit { TaskSpeedLimit() }

    public var headers: [String: Encodable]? { nil }

    public var method: Method { .get }

    public var contentType: ContentType { .json }

    public var parameters: [String: Any]? { nil }

    public var options: TaskOptions { TaskOptions(isDebug: __gearBox.debug, timeout: __gearBox.timeout) }

    public var proxy: (String, Int)? { __gearBox.proxy }
}

extension Race {
    @discardableResult
    public func race(completion: @escaping Result) -> CUnsignedInt {
        dataTask(
                method: method,
                url: url,
                contentType: contentType,
                headers: headers,
                options: options,
                speedLimit: speedLimit,
                proxy: proxy,
                param: parameters,
                completion: completion
        )
    }
}

//public protocol LongDistanceRace: Race {
//    var filePath: String { get }
//    var progress: ((_ now: UInt64, _ total: UInt64) -> Void)? { get }
//}
//
//extension LongDistanceRace {
//    public var progress: ((UInt64, UInt64) -> Void)? { nil }
//}
//
//public func longDistanceRace(race: LongDistanceRace, complete: @escaping (Result)) -> UInt32 {
//    assert((race.method == Method.download || race.method == Method.upload), "method must be download or upload")
//    return request(
//            method: race.method,
//            url: race.url,
//            contentType: race.contentType,
//            headers: race.headers,
//            timeOut: race.timeout,
//            speedLimit: race.speedLimit,
//            param: race.parameters,
//            downloadPath: race.filePath,
//            progress: race.progress,
//            completion: complete)
//}

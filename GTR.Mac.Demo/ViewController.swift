//
//  ViewController.swift
//  GTR.Mac.Demo
//
//  Created by Mengyu Li on 2019/8/7.
//  Copyright © 2019 limengyu.top. All rights reserved.
//

import Cocoa
import GTR

class ViewController: NSViewController {

    private lazy var actionButton = { () -> NSButton in
        let actionButton = NSButton(title: "Go", target: self, action: #selector(ViewController.getAction))
        actionButton.translatesAutoresizingMaskIntoConstraints = false
        return actionButton
    }()

    override func viewDidLoad() {
        super.viewDidLoad()

        // Do any additional setup after loading the view.
        self.setupUI()
        self.setupGTR()
    }

    override var representedObject: Any? {
        didSet {
            // Update the view, if already loaded.
        }
    }
}

extension ViewController {
    private func setupUI() {
        self.view.addSubview(self.actionButton)
        self.actionButton.leadingAnchor.constraint(equalTo: self.view.leadingAnchor).isActive = true
        self.actionButton.bottomAnchor.constraint(equalTo: self.view.bottomAnchor).isActive = true
        self.actionButton.trailingAnchor.constraint(equalTo: self.view.trailingAnchor).isActive = true
        self.actionButton.heightAnchor.constraint(equalToConstant: 60).isActive = true
    }

    private func setupGTR() {
        GTR.setup(agent: type(of: self), logger: type(of: self), configuration: type(of: self))
    }
}

extension ViewController {
    private func get() {
        GetDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
            case .failure(_):
                break
            }
        }
    }

    private func brotli() {
        BrotliDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
            case .failure(_):
                break
            }
        }
    }

    private func getCache() {
        GetCacheDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
            case .failure(_):
                break
            }
        }
    }

    private func post() {
        PostDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
                break
            case .failure(_):
                break
            }
        }
    }

    private func custom() {
        CustomDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
                break
            case .failure(_):
                break
            }
        }
    }

    private func proxy() {
        ProxyDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
                break
            case .failure(_):
                break
            }
        }
    }

    private func formData() {
        FormDataDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
                break
            case .failure(_):
                break
            }
        }
    }

    private func cache() {
        CacheDemo().race { destination in
            switch destination {
            case .success(let goal):
                print("\(goal)")
                break
            case .failure(_):
                break
            }
        }
    }
}

extension ViewController {
    @objc
    private func getAction() {
//        self.brotli()
//        self.getCache()
        self.post()
//        self.custom()
//        self.get()
//        self.proxy()
//        self.formData()
//        self.cache()
    }
}

extension ViewController: Driver {
    public static var identity: [String: CustomStringConvertible]? {
        ["Access-Token": "000347174c.db8743c66094ff2964a2b4b6791cf5db"]
    }
    public static var userAgent: String? {
        "GTRDemo/1.0.0"
    }
}

extension ViewController: Horn {
    public class func whistle(type: HornType, message: String, filename: String, function: String, line: Int) {
        print("[\(type.prefix)] => \(message)")
    }
}

extension ViewController: GearBox {
    //global proxy
//    public private(set) static var proxy: (String, Int)? = ("http://127.0.0.1", 1087)
    static let responseInfoOption: Option.ResponseInfo = Option.ResponseInfo(base: true, time: true, size: true, speed: true, ssl: true, socket: true, cookie: true)
}

//
//  HybridTextStream.swift
//  Pods
//
//  Created by Alex Shumihin on 10/10/2026.
//

import Foundation
import UIKit

class HybridTextStream : HybridTextStreamSpec {
  // UIView
  var view: UIView = UIView()

  // Props
  var isRed: Bool = false {
    didSet {
      view.backgroundColor = isRed ? .red : .black
    }
  }
}

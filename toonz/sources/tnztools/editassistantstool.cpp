
// TnzTools includes
#include <tools/tool.h>
#include <tools/toolutils.h>
#include <tools/toolhandle.h>
#include <tools/cursors.h>
#include <tools/assistant.h>
#include <tools/inputmanager.h>

// TnzLib includes
#include <toonz/tapplication.h>
#include <toonz/txshlevelhandle.h>

// TnzCore includes
#include <tgl.h>
#include <tproperty.h>
#include <tmetaimage.h>

// For Qt translation support
#include <QCoreApplication>

#include <map>


//-------------------------------------------------------------------

//=============================================================================
// Edit Assistants Undo
//-----------------------------------------------------------------------------

class EditAssistantsUndo final : public ToolUtils::TToolUndo {
private:
  bool m_isCreated;
  bool m_isRemoved;
  TMetaObjectP m_metaObject;
  TVariant m_oldData;
  TVariant m_newData;
  size_t m_size;

public:
  EditAssistantsUndo(
    TXshSimpleLevel *level,
    const TFrameId &frameId,
    bool isCreated,
    bool isRemoved,
    TMetaObjectP metaObject,
    TVariant oldData
  ):
    ToolUtils::TToolUndo(level, frameId),
    m_isCreated(isCreated),
    m_isRemoved(isRemoved),
    m_metaObject(metaObject),
    m_oldData(oldData),
    m_newData(m_metaObject->data()),
    m_size(m_oldData.getMemSize() + m_newData.getMemSize())
  { }

  int getSize() const override
    { return m_size; }
  QString getToolName() override
    { return QString("Edit Assistants Tool"); }

  void process(bool remove, const TVariant &data) const {
    if (TMetaImage *metaImage = dynamic_cast<TMetaImage*>(m_level->getFrame(m_frameId, true).getPointer()))
    {
      { // wrap writer
        TMetaImage::Writer writer(*metaImage);
        bool found = false;
        for(TMetaObjectList::iterator i = writer->begin(); i != writer->end(); ++i)
          if ((*i) == m_metaObject) {
            if (remove) writer->erase(i);
            found = true;
            break;
          }
        if (!remove) {
          if (!found)
            writer->push_back(m_metaObject);
          m_metaObject->data() = data;
          if (m_metaObject->handler())
            m_metaObject->handler()->fixData();
        }
      }
      notifyImageChanged();
    }
  }

  void undo() const override
    { process(m_isCreated, m_oldData); }

  void redo() const override
    { process(m_isRemoved, m_newData); }
};


//=============================================================================
// Edit Assistants Tool
//-----------------------------------------------------------------------------

class EditAssistantsTool final : public TTool {
  Q_DECLARE_TR_FUNCTIONS(EditAssistantsTool)
public:
  typedef std::map<TStringId, std::string> TypeMap;

  enum Mode {
    ModeImage,
    ModeAssistant,
    ModePoint
  };

protected:
  TPropertyGroup m_allProperties;
  TPropertyGroup m_toolProperties;
  TEnumProperty m_assistantType;
  TypeMap m_typeToName;
  TStringId m_newAssisnantType;

  bool           m_dragging;
  TMetaObjectH   m_currentAssistant;
  bool           m_currentAssistantCreated;
  bool           m_currentAssistantChanged;
  int            m_currentAssistantIndex;
  TVariant       m_currentAssistantBackup;
  int            m_currentPointIndex;
  TPointD        m_currentPointOffset;
  TPointD        m_currentPosition;
  TGuidelineList m_currentGuidelines;

  TMetaImage::Reader *m_reader;
  TMetaImage         *m_readImage;
  TMetaObjectPC       m_readObject;
  const TAssistant   *m_readAssistant;

  TMetaImage::Writer *m_writer;
  TMetaImage         *m_writeImage;
  TMetaObjectP        m_writeObject;
  TAssistant         *m_writeAssistant;

public:
  EditAssistantsTool():
    TTool("T_EditAssistants"),
    m_assistantType("AssistantType"),
    m_dragging(),
    m_currentAssistantCreated(),
    m_currentAssistantChanged(),
    m_currentAssistantIndex(-1),
    m_currentPointIndex(-1),
    m_reader(),
    m_readImage(),
    m_readAssistant(),
    m_writer(),
    m_writeImage(),
    m_writeAssistant()
  {
    bind(MetaImage);
    addAssistantType("", "--");
    addAssistantType("assistantVanishingPoint", "Vanishing Point");
    m_toolProperties.bind(m_assistantType);
    updateTranslation();
  }

  ~EditAssistantsTool()
    { close(); }

  ToolType getToolType() const override
    { return TTool::LevelWriteTool; }
  int getCursorId() const override
    { return ToolCursor::StrokeSelectCursor; }
  void onImageChanged() override
    { getViewer()->GLInvalidateAll(); }

  TPropertyGroup* getProperties(int) override {
    m_allProperties.clear();
    for(int i = 0; i < m_toolProperties.getPropertyCount(); ++i)
      m_allProperties.bind( *m_toolProperties.getProperty(i) );
    if (Closer closer = read(ModeAssistant)) {
      TPropertyGroup &assistantProperties = m_readAssistant->getProperties();
      for(int i = 0; i < assistantProperties.getPropertyCount(); ++i)
        m_allProperties.bind( *assistantProperties.getProperty(i) );
    }
    updateTranslation();
    return &m_allProperties;
  }

  void addAssistantType(const std::string &typeName, const std::string &name) {
    if (m_typeToName.insert( TypeMap::value_type(TStringId(typeName), name) ).second)
      m_assistantType.addValueWithUIName(
        to_wstring(typeName),
        QString::fromStdString(name) );
  }

  void updateTranslation() override {
    for(TypeMap::const_iterator i = m_typeToName.begin(); i != m_typeToName.end(); ++i)
      m_assistantType.setItemUIName(
        to_wstring(i->first.str()),
        tr(i->second.c_str()) );
    for(int i = 0; i < m_allProperties.getPropertyCount(); ++i) {
      TProperty *p = m_allProperties.getProperty(i);
      //p->setQStringName( tr( p->getUINameOrig().c_str() ) );
    }
  }

  bool onPropertyChanged(std::string name/*, bool addToUndo*/) override {
    if (TProperty *property = m_toolProperties.getProperty(name)) {
      if (name == m_assistantType.getName())
        m_newAssisnantType = TStringId::find( to_string(m_assistantType.getValue()) );
    } else {
      if (Closer closer = write(ModeAssistant, true))
        m_writeAssistant->propertyChanged(TStringId::find(name));
      //if (addToUndo) apply();
      getViewer()->GLInvalidateAll();
    }
    return true;
  }

protected:
  void close() {
    m_readAssistant = 0;
    m_readObject.reset();
    m_readImage = 0;
    if (m_reader) delete(m_reader);
    m_reader = 0;

    m_writeAssistant = 0;
    m_writeObject.reset();
    m_writeImage = 0;
    if (m_writer) delete(m_writer);
    m_writer = 0;
  }

  bool openRead(Mode mode) {
    close();

    if ( (mode >= ModeAssistant && !m_currentAssistant)
      || (mode >= ModeAssistant && m_currentAssistantIndex < 0)
      || (mode >= ModePoint && m_currentPointIndex < 0) ) return false;

    m_readImage = dynamic_cast<TMetaImage*>(getImage(true));
    if (m_readImage) {
      m_reader = new TMetaImage::Reader(*m_readImage);
      if (mode == ModeImage) return true;

      if ( m_currentAssistantIndex < (int)(*m_reader)->size()
        && (**m_reader)[m_currentAssistantIndex] == m_currentAssistant )
      {
        m_readObject = (**m_reader)[m_currentAssistantIndex];
        m_readAssistant = m_readObject->getHandler<TAssistant>();
        if (mode == ModeAssistant) return true;

        if (m_currentPointIndex < m_readAssistant->pointsCount()) {
          if (mode == ModePoint) return true;
        }
      }
    }

    close();
    return false;
  }

  void touch() {
    if (m_writeAssistant && !m_currentAssistantChanged) {
      m_currentAssistantBackup = m_writeAssistant->data();
      m_currentAssistantChanged = true;
    }
  }

  bool openWrite(Mode mode, bool touch = false) {
    close();

    if ( (mode >= ModeAssistant && !m_currentAssistant)
      || (mode >= ModeAssistant && m_currentAssistantIndex < 0)
      || (mode >= ModePoint && m_currentPointIndex < 0) ) return false;

    m_writeImage = dynamic_cast<TMetaImage*>(getImage(true));
    if (m_writeImage) {
      m_writer = new TMetaImage::Writer(*m_writeImage);
      if (mode == ModeImage) return true;

      if ( m_currentAssistantIndex < (int)(*m_writer)->size()
        && (**m_writer)[m_currentAssistantIndex] == m_currentAssistant )
      {
        m_writeObject = (**m_writer)[m_currentAssistantIndex];
        m_writeAssistant = m_writeObject->getHandler<TAssistant>();
        if ( (mode == ModeAssistant)
          || (mode == ModePoint && m_currentPointIndex < m_writeAssistant->pointsCount()) )
        {
          if (touch) this->touch();
          return true;
        }
      }
    }

    close();
    return false;
  }


  //! helper functions for construction like this:
  //!   if (Closer closer = read(ModeAssistant)) { do-something... }
  struct Closer {
    struct Args {
      EditAssistantsTool *owner;
      Args(EditAssistantsTool &owner): owner(&owner) { }
      operator bool() const //!< declare bool-convertor here to prevent convertion path: Args->Closer->bool
        { return owner && (owner->m_reader || owner->m_writer); }
      void close()
        { if (owner) owner->close(); }
    };
    Closer(const Args &args): args(args) { }
    ~Closer() { args.close(); }
    operator bool() const { return args; }
  private:
    Args args;
  };

  Closer::Args read(Mode mode)
    { openRead(mode); return Closer::Args(*this); }
  Closer::Args write(Mode mode, bool touch = false)
    { openWrite(mode, touch); return Closer::Args(*this); }

public:
  void updateOptionsBox()
    { getApplication()->getCurrentTool()->notifyToolOptionsBoxChanged(); }

  void resetCurrentPoint(bool updateOptionsBox = true) {
    close();
    m_currentAssistant.reset();
    m_currentAssistantCreated = false;
    m_currentAssistantChanged = false;
    m_currentAssistantIndex = -1;
    m_currentPointIndex = -1;
    m_currentPointOffset = TPointD();
    m_currentAssistantBackup.reset();
    if (updateOptionsBox) this->updateOptionsBox();
  }

  bool findCurrentPoint(const TPointD &position, double pixelSize = 1, bool updateOptionsBox = true) {
    resetCurrentPoint(false);
    if (Closer closer = read(ModeImage)) {
      for(TMetaObjectListCW::iterator i = (*m_reader)->begin(); i != (*m_reader)->end(); ++i) {
        if (!*i) continue;

        const TAssistant *assistant = (*i)->getHandler<TAssistant>();
        if (!assistant) continue;

        assistant->deselectAll();
        for(int j = 0; j < assistant->pointsCount() && m_currentAssistantIndex < 0; ++j) {
          const TAssistantPoint &p = assistant->points()[j];
          TPointD offset = p.position - position;
          if (norm(offset) <= p.radius*pixelSize) {
            m_currentAssistant.set(*i);
            m_currentAssistantIndex = i - (*m_reader)->begin();
            m_currentPointIndex = j;
            m_currentPointOffset = offset;
            assistant->selectAll();
          }
        }
      }
    }

    if (updateOptionsBox) this->updateOptionsBox();
    return m_currentAssistantIndex >= 0;
  }

  bool apply() {
    bool success = false;
    if (m_currentAssistantChanged || m_currentAssistantCreated) {
      if (Closer closer = write(ModePoint)) {
        m_writeAssistant->fixData();
        TUndoManager::manager()->add(new EditAssistantsUndo(
          getApplication()->getCurrentLevel()->getLevel()->getSimpleLevel(),
          getCurrentFid(),
          m_currentAssistantCreated,
          false,
          m_writeObject,
          m_currentAssistantBackup ));
        m_currentAssistantCreated = false;
        m_currentAssistantChanged = false;
        success = true;
      }
    }

    if (success) {
      notifyImageChanged();
      getApplication()->getCurrentTool()->notifyToolChanged();
      getViewer()->GLInvalidateAll();
    }

    return success;
  }

  void leftButtonDown(const TPointD &position, const TMouseEvent&) override {
    apply();
    m_dragging = true;
    if (m_newAssisnantType) {
      // create assistant
      resetCurrentPoint();
      if (Closer closer = write(ModeImage)) {
        TMetaObjectP object(new TMetaObject(m_newAssisnantType));
        if (TAssistant *assistant = object->getHandler<TAssistant>()) {
          if (assistant->pointsCount()) {
            assistant->setDefaults();
            assistant->movePoint(0, position);
            m_currentAssistantCreated = true;
            m_currentAssistantChanged = true;
            m_currentAssistantIndex = (int)(*m_writer)->size();
            m_currentAssistant = object;
            m_currentPointIndex = 0;
            m_currentPointOffset = TPointD();
            m_currentAssistantBackup = assistant->data();
          }
          (*m_writer)->push_back(object);
        }
      }
      m_newAssisnantType.reset();
    } else {
      findCurrentPoint(position);
    }

    m_currentPosition = position;
    getViewer()->GLInvalidateAll();
  }

  void leftButtonDrag(const TPointD &position, const TMouseEvent&) override {
    if (Closer closer = write(ModePoint, true))
      m_writeAssistant->movePoint(
        m_currentPointIndex,
        position + m_currentPointOffset);
    m_currentPosition = position;
    getViewer()->GLInvalidateAll();
  }

  void leftButtonUp(const TPointD &position, const TMouseEvent&) override {
    if (Closer closer = write(ModePoint, true))
      m_writeAssistant->movePoint(
        m_currentPointIndex,
        position + m_currentPointOffset);

    apply();
    m_assistantType.setIndex(0);
    getApplication()->getCurrentTool()->notifyToolChanged();
    emit getApplication()->getCurrentTool()->toolChanged();
    m_currentPosition = position;
    getViewer()->GLInvalidateAll();
    m_dragging = false;
  }

  bool keyDown(QKeyEvent *keyEvent) override {
    if (keyEvent->key() == Qt::Key_Delete) {
      if (!m_dragging) {
        apply();
        bool success = false;
        if (Closer closer = write(ModeAssistant, true)) {
          (*m_writer)->erase((*m_writer)->begin() + m_currentAssistantIndex);
          TUndoManager::manager()->add(new EditAssistantsUndo(
            getApplication()->getCurrentLevel()->getLevel()->getSimpleLevel(),
            getCurrentFid(),
            false,
            true,
            m_writeObject,
            m_writeObject->data() ));
          success = true;
        }

        if (success) {
          notifyImageChanged();
        }

        resetCurrentPoint();
        getApplication()->getCurrentTool()->notifyToolChanged();
        getViewer()->GLInvalidateAll();
      }
      return true;
    }
    return false;
  }

  void draw() override {
    m_currentGuidelines.clear();

    // draw assistants
    if (Closer closer = read(ModeImage))
    for(TMetaObjectListCW::iterator i = (*m_reader)->begin(); i != (*m_reader)->end(); ++i)
      if (*i)
      if (const TAssistant *assistant = (*i)->getHandler<TAssistant>())
      {
        assistant->drawEdit(getViewer());
        assistant->getGuidelines(
          m_currentPosition + m_currentPointOffset,
          TAffine(),
          m_currentGuidelines );
      }

    // draw guidelines
    for(TGuidelineList::const_iterator i = m_currentGuidelines.begin(); i != m_currentGuidelines.end(); ++i)
      (*i)->draw();
  }
};

//-------------------------------------------------------------------

EditAssistantsTool editAssistantsTool;

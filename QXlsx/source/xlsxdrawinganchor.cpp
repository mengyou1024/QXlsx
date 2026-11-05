// xlsxdrawinganchor.cpp

#include "xlsxchart.h"
#include "xlsxdrawing_p.h"
#include "xlsxdrawinganchor_p.h"
#include "xlsxmediafile_p.h"
#include "xlsxutility_p.h"
#include "xlsxworkbook.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

QT_BEGIN_NAMESPACE_XLSX

/*
     The vertices that define the position of a graphical object
     within the worksheet in pixels.

             +------------+------------+
             |     A      |      B     |
       +-----+------------+------------+
       |     |(x1,y1)     |            |
       |  1  |(A1)._______|______      |
       |     |    |              |     |
       |     |    |              |     |
       +-----+----|    OBJECT    |-----+
       |     |    |              |     |
       |  2  |    |______________.     |
       |     |            |        (B2)|
       |     |            |     (x2,y2)|
       +---- +------------+------------+

     Example of an object that covers some of the area from cell A1 to  B2.

     Based on the width and height of the object we need to calculate 8 vars:

         col_start, row_start, col_end, row_end, x1, y1, x2, y2.

     We also calculate the absolute x and y position of the top left vertex of
     the object. This is required for images.

     The width and height of the cells that the object occupies can be
     variable and have to be taken into account.
*/

// anchor

DrawingAnchor::DrawingAnchor(Drawing *drawing, ObjectType objectType)
    : m_drawing(drawing)
    , m_objectType(objectType)
{
    m_drawing->anchors.append(this);
    m_id = m_drawing->anchors.size(); // must be unique in one drawing{x}.xml file.
}

DrawingAnchor::~DrawingAnchor()
{
}

void DrawingAnchor::setObjectPicture(const QImage &img)
{
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    m_pictureFile =
        std::make_shared<MediaFile>(ba, QStringLiteral("png"), QStringLiteral("image/png"));
    m_drawing->workbook->addMediaFile(m_pictureFile);

    m_objectType = Picture;
}

bool DrawingAnchor::getObjectPicture(QImage &img)
{
    if (m_pictureFile == nullptr)
        return false;

    bool ret = img.loadFromData(m_pictureFile->contents());
    return ret;
}

//{{ liufeijin
void DrawingAnchor::setObjectShape(const QImage &img)
{
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    m_pictureFile =
        std::make_shared<MediaFile>(ba, QStringLiteral("png"), QStringLiteral("image/png"));
    m_drawing->workbook->addMediaFile(m_pictureFile);

    m_objectType = Shape;
}
//}}

void DrawingAnchor::setObjectGraphicFrame(std::shared_ptr<Chart> chart)
{
    m_chartFile = chart;
    m_drawing->workbook->addChartFile(chart);

    m_objectType = GraphicFrame;
}

int DrawingAnchor::row() const
{
    return -1;
}

int DrawingAnchor::col() const
{
    return -1;
}

QPoint DrawingAnchor::loadXmlPos(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("pos") || reader.name() == QLatin1String("off"));

    QPoint pos;
    QXmlStreamAttributes attrs = reader.attributes();
    pos.setX(attrs.value(QLatin1String("x")).toInt());
    pos.setY(attrs.value(QLatin1String("y")).toInt());
    return pos;
}

QSize DrawingAnchor::loadXmlExt(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("ext"));

    QSize size;
    QXmlStreamAttributes attrs = reader.attributes();
    size.setWidth(attrs.value(QLatin1String("cx")).toInt());
    size.setHeight(attrs.value(QLatin1String("cy")).toInt());
    return size;
}

XlsxMarker DrawingAnchor::loadXmlMarker(QXmlStreamReader &reader, const QString &node)
{
    Q_ASSERT(reader.name() == node);

    int col       = 0;
    int colOffset = 0;
    int row       = 0;
    int rowOffset = 0;
    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("col")) {
                col = reader.readElementText().toInt();
            } else if (reader.name() == QLatin1String("colOff")) {
                colOffset = reader.readElementText().toInt();
            } else if (reader.name() == QLatin1String("row")) {
                row = reader.readElementText().toInt();
            } else if (reader.name() == QLatin1String("rowOff")) {
                rowOffset = reader.readElementText().toInt();
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement && reader.name() == node) {
            break;
        }
    }

    return XlsxMarker(row, col, rowOffset, colOffset);
}

void DrawingAnchor::loadXmlObject(QXmlStreamReader &reader)
{
    /*
    <xsd:group name="EG_ObjectChoices">
        <xsd:sequence>
            <xsd:choice minOccurs="1" maxOccurs="1">
                <xsd:element name="sp" type="CT_Shape"/>
                <xsd:element name="grpSp" type="CT_GroupShape"/>
                <xsd:element name="graphicFrame" type="CT_GraphicalObjectFrame"/>
                <xsd:element name="cxnSp" type="CT_Connector"/>
                <xsd:element name="pic" type="CT_Picture"/>
                <xsd:element name="contentPart" type="CT_Rel"/>
            </xsd:choice>
        </xsd:sequence>
    </xsd:group>
    */

    if (reader.name() == QLatin1String("sp")) // <xsd:element name="sp" type="CT_Shape"/>
    {
        // Shape
        m_objectType = Shape;

        //{{ liufeijin
        sp_textlink = reader.attributes().value(QLatin1String("textlink")).toString();
        sp_macro    = reader.attributes().value(QLatin1String("macro")).toString();
        //}}

        // <xsd:attribute name="macro" type="xsd:string" use="optional"/>
        // <xsd:attribute name="textlink" type="xsd:string" use="optional"/>
        // <xsd:attribute name="fLocksText" type="xsd:boolean" use="optional" default="true"/>
        // <xsd:attribute name="fPublished" type="xsd:boolean" use="optional" default="false"/>

        loadXmlObjectShape(reader); // CT_Shape
    } else if (reader.name() ==
               QLatin1String("grpSp")) // <xsd:element name="grpSp" type="CT_GroupShape"/>
    {
        // Group Shape
        m_objectType = GroupShape;
        loadXmlObjectGroupShape(reader);
    } else if (reader.name() == QLatin1String("graphicFrame")) // <xsd:element name="graphicFrame"
                                                               // type="CT_GraphicalObjectFrame"/>
    {
        // Graphic Frame
        m_objectType = GraphicFrame;
        loadXmlObjectGraphicFrame(reader);
    } else if (reader.name() ==
               QLatin1String("cxnSp")) // <xsd:element name="cxnSp" type="CT_Connector"/>
    {
        // Connection Shape
        m_objectType = ConnectionShape;

        // {{ liufeijin
        cxnSp_macro = reader.attributes().value(QLatin1String("macro")).toString();
        // }}

        loadXmlObjectConnectionShape(reader);
    } else if (reader.name() == QLatin1String("pic")) // <xsd:element name="pic" type="CT_Picture"/>
    {
        // Picture
        m_objectType = Picture;
        loadXmlObjectPicture(reader);
    } else if (reader.name() ==
               QLatin1String("contentPart")) // <xsd:element name="contentPart" type="CT_Rel"/>
    {
        // contentPart
        /// TODO:
    }
}

void DrawingAnchor::loadXmlObjectConnectionShape(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("cxnSp"));
    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() != QXmlStreamReader::StartElement) {
            if (reader.tokenType() == QXmlStreamReader::EndElement &&
                reader.name() == QLatin1String("cxnSp")) {
                break;
            }
            continue;
        }

        if (reader.name() == QLatin1String("nvCxnSpPr")) {
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    if (reader.name() == QLatin1String("cNvPr")) {
                        xsp_cNvPR_name = reader.attributes().value(QLatin1String("name")).toString();
                        xsp_cNvPR_id   = reader.attributes().value(QLatin1String("id")).toString();
                        reader.skipCurrentElement();
                    } else if (reader.name() == QLatin1String("cNvCxnSpPr")) {
                        reader.skipCurrentElement();
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("nvCxnSpPr")) {
                    break;
                }
            }
        } else if (reader.name() == QLatin1String("spPr")) {
            xbwMode = reader.attributes().value(QLatin1String("bwMode")).toString();
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    if (reader.name() == QLatin1String("xfrm")) {
                        cxnSp_filpV = reader.attributes().value(QLatin1String("flipV")).toString();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("off")) {
                                    posTA = loadXmlPos(reader);
                                    reader.skipCurrentElement();
                                } else if (reader.name() == QLatin1String("ext")) {
                                    extTA = loadXmlExt(reader);
                                    reader.skipCurrentElement();
                                } else {
                                    reader.skipCurrentElement();
                                }
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                                       reader.name() == QLatin1String("xfrm")) {
                                break;
                            }
                        }
                    } else if (reader.name() == QLatin1String("prstGeom")) {
                        xprstGeom_prst =
                            reader.attributes().value(QLatin1String("prst")).toString().trimmed();
                        reader.skipCurrentElement();
                    } else if (reader.name() == QLatin1String("ln")) {
                        xIn_algn = reader.attributes().value(QLatin1String("algn")).toString().trimmed();
                        xIn_cmpd = reader.attributes().value(QLatin1String("cmpd")).toString().trimmed();
                        xIn_cap  = reader.attributes().value(QLatin1String("cap")).toString().trimmed();
                        xIn_w    = reader.attributes().value(QLatin1String("w")).toString().trimmed();
                        reader.skipCurrentElement(); // 跳过 headEnd/tailEnd 等嵌套
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("spPr")) {
                    break;
                }
            }
        } else if (reader.name() == QLatin1String("style")) {
            // style 内部：lnRef/fillRef/effectRef/fontRef，每个下有 schemeClr val
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    QString *idxTarget = nullptr;
                    QString *valTarget = nullptr;
                    if (reader.name() == QLatin1String("lnRef")) {
                        idxTarget = &Style_inref_idx;
                        valTarget = &Style_inref_val;
                    } else if (reader.name() == QLatin1String("fillRef")) {
                        idxTarget = &style_fillref_idx;
                        valTarget = &style_fillref_val;
                    } else if (reader.name() == QLatin1String("effectRef")) {
                        idxTarget = &style_effectref_idx;
                        valTarget = &style_effectref_val;
                    } else if (reader.name() == QLatin1String("fontRef")) {
                        idxTarget = &style_forntref_idx;
                        valTarget = &style_forntref_val;
                    }
                    if (idxTarget) {
                        *idxTarget =
                            reader.attributes().value(QLatin1String("idx")).toString().trimmed();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("schemeClr") ||
                                    reader.name() == QLatin1String("scrgbClr") ||
                                    reader.name() == QLatin1String("srgbClr")) {
                                    *valTarget = reader.attributes()
                                                     .value(QLatin1String("val"))
                                                     .toString()
                                                     .trimmed();
                                }
                                reader.skipCurrentElement();
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement) {
                                break;
                            }
                        }
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("style")) {
                    break;
                }
            }
        } else {
            reader.skipCurrentElement();
        }
    }
}

void DrawingAnchor::loadXmlObjectGraphicFrame(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("graphicFrame"));

    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("chart")) {
                QString rId  = reader.attributes().value(QLatin1String("r:id")).toString();
                QString name = m_drawing->relationships()->getRelationshipById(rId).target;

                const auto parts = splitPath(m_drawing->filePath());
                QString path     = QDir::cleanPath(parts.first() + QLatin1String("/") + name);

                bool exist                        = false;
                QList<std::shared_ptr<Chart>> cfs = m_drawing->workbook->chartFiles();
                for (int i = 0; i < cfs.size(); ++i) {
                    if (cfs[i]->filePath() == path) {
                        // already exist
                        exist       = true;
                        m_chartFile = cfs[i];
                    }
                }
                if (!exist) {
                    m_chartFile = std::shared_ptr<Chart>(
                        new Chart(m_drawing->sheet, Chart::F_LoadFromExists));
                    m_chartFile->setFilePath(path);
                    m_drawing->workbook->addChartFile(m_chartFile);
                }
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("graphicFrame")) {
            break;
        }
    }
}

void DrawingAnchor::loadXmlObjectGroupShape(QXmlStreamReader &reader)
{
    Q_UNUSED(reader)
}

void DrawingAnchor::loadXmlObjectPicture(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("pic"));

    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("nvPicPr")) {
                // nvPicPr 内部：cNvPr(id/name)、cNvPicPr
                while (!reader.atEnd()) {
                    reader.readNextStartElement();
                    if (reader.tokenType() == QXmlStreamReader::StartElement) {
                        if (reader.name() == QLatin1String("cNvPr")) {
                            xsp_cNvPR_id =
                                reader.attributes().value(QLatin1String("id")).toString();
                            xsp_cNvPR_name =
                                reader.attributes().value(QLatin1String("name")).toString();
                        }
                        reader.skipCurrentElement();
                    } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                               reader.name() == QLatin1String("nvPicPr")) {
                        break;
                    }
                }
            } else if (reader.name() == QLatin1String("blipFill")) {
                rotWithShapeTA =
                    reader.attributes().value(QLatin1String("rotWithShape")).toString().toInt();
                dpiTA = reader.attributes().value(QLatin1String("dpi")).toString().toInt();
                while (!reader.atEnd()) {
                    reader.readNextStartElement();
                    if (reader.tokenType() == QXmlStreamReader::StartElement) {
                        if (reader.name() == QLatin1String("blip")) {
                            QString rId =
                                reader.attributes().value(QLatin1String("r:embed")).toString();
                            sp_blip_cstate =
                                reader.attributes().value(QLatin1String("cstate")).toString();
                            sp_blip_rembed = rId;
                            QString name =
                                m_drawing->relationships()->getRelationshipById(rId).target;
                            const auto parts = splitPath(m_drawing->filePath());
                            QString path =
                                QDir::cleanPath(parts.first() + QLatin1String("/") + name);

                            bool exist     = false;
                            const auto mfs = m_drawing->workbook->mediaFiles();
                            for (const auto &mf : mfs) {
                                if (mf->fileName() == path) {
                                    exist         = true;
                                    m_pictureFile = mf;
                                }
                            }
                            if (!exist) {
                                m_pictureFile = std::make_shared<MediaFile>(path);
                                m_drawing->workbook->addMediaFile(m_pictureFile, true);
                            }
                        }
                        reader.skipCurrentElement();
                    } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                               reader.name() == QLatin1String("blipFill")) {
                        break;
                    }
                }
            } else if (reader.name() == QLatin1String("spPr")) {
                // spPr 内部：xfrm(off/ext)、prstGeom
                while (!reader.atEnd()) {
                    reader.readNextStartElement();
                    if (reader.tokenType() == QXmlStreamReader::StartElement) {
                        if (reader.name() == QLatin1String("xfrm")) {
                            while (!reader.atEnd()) {
                                reader.readNextStartElement();
                                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                    if (reader.name() == QLatin1String("off")) {
                                        posTA.setX(reader.attributes()
                                                       .value(QLatin1String("x"))
                                                       .toString()
                                                       .toInt());
                                        posTA.setY(reader.attributes()
                                                       .value(QLatin1String("y"))
                                                       .toString()
                                                       .toInt());
                                    } else if (reader.name() == QLatin1String("ext")) {
                                        extTA.setWidth(reader.attributes()
                                                           .value(QLatin1String("cx"))
                                                           .toString()
                                                           .toInt());
                                        extTA.setHeight(reader.attributes()
                                                            .value(QLatin1String("cy"))
                                                            .toString()
                                                            .toInt());
                                    }
                                    reader.skipCurrentElement();
                                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                                           reader.name() == QLatin1String("xfrm")) {
                                    break;
                                }
                            }
                        } else if (reader.name() == QLatin1String("prstGeom")) {
                            xprstGeom_prst =
                                reader.attributes().value(QLatin1String("prst")).toString();
                            reader.skipCurrentElement();
                        } else {
                            reader.skipCurrentElement();
                        }
                    } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                               reader.name() == QLatin1String("spPr")) {
                        break;
                    }
                }
            } else {
                reader.skipCurrentElement();
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("pic")) {
            break;
        }
    }
}

// 读取 reader 当前所在元素的完整 XML 子树（从当前 StartElement 到其 EndElement），
// 返回包含该元素及所有子节点的原始 XML 字符串。用于 txBody 等需要整段 roundtrip 的场景。
// 函数退出时 reader 停在当前元素的 EndElement token 上。
static QString readElementTreeRaw(QXmlStreamReader &reader)
{
    QString result;
    QXmlStreamWriter writer(&result);
    writer.setAutoFormatting(false);

    // 写入当前 StartElement（含属性）
    writer.writeStartElement(reader.qualifiedName().toString());
    const QXmlStreamAttributes attrs = reader.attributes();
    for (const auto &a : attrs) {
        writer.writeAttribute(a.qualifiedName().toString(), a.value().toString());
    }

    int depth = 1;
    while (!reader.atEnd() && depth > 0) {
        reader.readNext();
        switch (reader.tokenType()) {
        case QXmlStreamReader::StartElement: {
            writer.writeStartElement(reader.qualifiedName().toString());
            const QXmlStreamAttributes as = reader.attributes();
            for (const auto &a : as) {
                writer.writeAttribute(a.qualifiedName().toString(), a.value().toString());
            }
            ++depth;
            break;
        }
        case QXmlStreamReader::EndElement:
            writer.writeEndElement();
            --depth;
            break;
        case QXmlStreamReader::Characters:
            if (!reader.isWhitespace())
                writer.writeCharacters(reader.text().toString());
            break;
        case QXmlStreamReader::Comment:
            writer.writeComment(reader.text().toString());
            break;
        case QXmlStreamReader::DTD:
            writer.writeDTD(reader.text().toString());
            break;
        case QXmlStreamReader::EntityReference:
            writer.writeEntityReference(reader.name().toString());
            break;
        case QXmlStreamReader::ProcessingInstruction:
            writer.writeProcessingInstruction(reader.processingInstructionTarget().toString(),
                                               reader.processingInstructionData().toString());
            break;
        default:
            break;
        }
    }
    // 最外层 EndElement 已在循环里由 writer.writeEndElement() 处理，此处不再重复写
    return result;
}

void DrawingAnchor::loadXmlObjectShape(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("sp"));

    // 读取 sp 根元素属性
    sp_macro    = reader.attributes().value(QLatin1String("macro")).toString();
    sp_textlink = reader.attributes().value(QLatin1String("textlink")).toString();

    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() != QXmlStreamReader::StartElement) {
            if (reader.tokenType() == QXmlStreamReader::EndElement &&
                reader.name() == QLatin1String("sp")) {
                break;
            }
            continue;
        }

        if (reader.name() == QLatin1String("nvSpPr")) {
            // nvSpPr 内部：cNvPr（id/name）+ cNvSpPr（txBox）
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    if (reader.name() == QLatin1String("cNvPr")) {
                        xsp_cNvPR_id   = reader.attributes().value(QLatin1String("id")).toString();
                        xsp_cNvPR_name = reader.attributes().value(QLatin1String("name")).toString();
                        reader.skipCurrentElement();
                    } else if (reader.name() == QLatin1String("cNvSpPr")) {
                        sp_txBox = reader.attributes().value(QLatin1String("txBox")).toString();
                        reader.skipCurrentElement();
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("nvSpPr")) {
                    break;
                }
            }
        } else if (reader.name() == QLatin1String("spPr")) {
            xbwMode = reader.attributes().value(QLatin1String("bwMode")).toString();
            // 深入 spPr 内部
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    if (reader.name() == QLatin1String("xfrm")) {
                        cxnSp_filpV = reader.attributes().value(QLatin1String("flipV")).toString();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("off")) {
                                    posTA = loadXmlPos(reader);
                                    reader.skipCurrentElement();
                                } else if (reader.name() == QLatin1String("ext")) {
                                    extTA = loadXmlExt(reader);
                                    reader.skipCurrentElement();
                                } else {
                                    reader.skipCurrentElement();
                                }
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                                       reader.name() == QLatin1String("xfrm")) {
                                break;
                            }
                        }
                    } else if (reader.name() == QLatin1String("prstGeom")) {
                        xprstGeom_prst =
                            reader.attributes().value(QLatin1String("prst")).toString().trimmed();
                        reader.skipCurrentElement();
                    } else if (reader.name() == QLatin1String("blipFill")) {
                        rotWithShapeTA =
                            reader.attributes().value(QLatin1String("rotWithShape")).toString().toInt();
                        dpiTA = reader.attributes().value(QLatin1String("dpi")).toString().toInt();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("blip")) {
                                    sp_blip_rembed =
                                        reader.attributes().value(QLatin1String("r:embed")).toString();
                                    sp_blip_cstate =
                                        reader.attributes().value(QLatin1String("cstate")).toString();
                                    // 关联 mediaFile
                                    QString name = m_drawing->relationships()
                                                       ->getRelationshipById(sp_blip_rembed)
                                                       .target;
                                    QString path = QDir::cleanPath(
                                        splitPath(m_drawing->filePath())[0] +
                                        QLatin1String("/") + name);
                                    bool exist     = false;
                                    const auto mfs = m_drawing->workbook->mediaFiles();
                                    for (const auto &mf : mfs) {
                                        if (mf->fileName() == path) {
                                            exist         = true;
                                            m_pictureFile = mf;
                                        }
                                    }
                                    if (!exist) {
                                        m_pictureFile = std::make_shared<MediaFile>(path);
                                        m_drawing->workbook->addMediaFile(m_pictureFile, true);
                                    }
                                    reader.skipCurrentElement();
                                } else {
                                    reader.skipCurrentElement();
                                }
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                                       reader.name() == QLatin1String("blipFill")) {
                                break;
                            }
                        }
                    } else if (reader.name() == QLatin1String("ln")) {
                        xIn_algn = reader.attributes().value(QLatin1String("algn")).toString().trimmed();
                        xIn_cmpd = reader.attributes().value(QLatin1String("cmpd")).toString().trimmed();
                        xIn_cap  = reader.attributes().value(QLatin1String("cap")).toString().trimmed();
                        xIn_w    = reader.attributes().value(QLatin1String("w")).toString().trimmed();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("headEnd")) {
                                    x_headEnd_tyep =
                                        reader.attributes().value(QLatin1String("type")).toString().trimmed();
                                    x_headEnd_w =
                                        reader.attributes().value(QLatin1String("w")).toString().trimmed();
                                    x_headEnd_len =
                                        reader.attributes().value(QLatin1String("len")).toString().trimmed();
                                    reader.skipCurrentElement();
                                } else if (reader.name() == QLatin1String("tailEnd")) {
                                    x_tailEnd_tyep =
                                        reader.attributes().value(QLatin1String("type")).toString().trimmed();
                                    x_tailEnd_w =
                                        reader.attributes().value(QLatin1String("w")).toString().trimmed();
                                    x_tailEnd_len =
                                        reader.attributes().value(QLatin1String("len")).toString().trimmed();
                                    reader.skipCurrentElement();
                                } else {
                                    reader.skipCurrentElement();
                                }
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                                       reader.name() == QLatin1String("ln")) {
                                break;
                            }
                        }
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("spPr")) {
                    break;
                }
            }
        } else if (reader.name() == QLatin1String("style")) {
            // style 内部：lnRef/fillRef/effectRef/fontRef，每个下有 schemeClr val
            while (!reader.atEnd()) {
                reader.readNextStartElement();
                if (reader.tokenType() == QXmlStreamReader::StartElement) {
                    QString idx;
                    QString *valTarget = nullptr;
                    QString *idxTarget = nullptr;
                    if (reader.name() == QLatin1String("lnRef")) {
                        idxTarget = &Style_inref_idx;
                        valTarget = &Style_inref_val;
                    } else if (reader.name() == QLatin1String("fillRef")) {
                        idxTarget = &style_fillref_idx;
                        valTarget = &style_fillref_val;
                    } else if (reader.name() == QLatin1String("effectRef")) {
                        idxTarget = &style_effectref_idx;
                        valTarget = &style_effectref_val;
                    } else if (reader.name() == QLatin1String("fontRef")) {
                        idxTarget = &style_forntref_idx;
                        valTarget = &style_forntref_val;
                    }
                    if (idxTarget) {
                        *idxTarget = reader.attributes().value(QLatin1String("idx")).toString().trimmed();
                        while (!reader.atEnd()) {
                            reader.readNextStartElement();
                            if (reader.tokenType() == QXmlStreamReader::StartElement) {
                                if (reader.name() == QLatin1String("schemeClr") ||
                                    reader.name() == QLatin1String("scrgbClr") ||
                                    reader.name() == QLatin1String("srgbClr")) {
                                    *valTarget =
                                        reader.attributes().value(QLatin1String("val")).toString().trimmed();
                                }
                                reader.skipCurrentElement();
                            } else if (reader.tokenType() == QXmlStreamReader::EndElement) {
                                break; // 回到 lnRef/fillRef 等的 EndElement
                            }
                        }
                    } else {
                        reader.skipCurrentElement();
                    }
                } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                           reader.name() == QLatin1String("style")) {
                    break;
                }
            }
        } else if (reader.name() == QLatin1String("txBody")) {
            // 整段保存 txBody 子树 XML，save 时原样写回
            sp_txBodyXml = readElementTreeRaw(reader);
            // readElementTreeRaw 内部已经消费到 txBody 的 EndElement，此处 reader 应在 EndElement 上
        } else {
            reader.skipCurrentElement();
        }
    }
}

void DrawingAnchor::saveXmlPos(QXmlStreamWriter &writer, const QPoint &pos) const
{
    writer.writeEmptyElement(QStringLiteral("xdr:pos"));
    writer.writeAttribute(QStringLiteral("x"), QString::number(pos.x()));
    writer.writeAttribute(QStringLiteral("y"), QString::number(pos.y()));
}

void DrawingAnchor::saveXmlExt(QXmlStreamWriter &writer, const QSize &ext) const
{
    writer.writeStartElement(QStringLiteral("xdr:ext"));
    writer.writeAttribute(QStringLiteral("cx"), QString::number(ext.width()));
    writer.writeAttribute(QStringLiteral("cy"), QString::number(ext.height()));
    writer.writeEndElement(); // xdr:ext
}

void DrawingAnchor::saveXmlMarker(QXmlStreamWriter &writer,
                                  const XlsxMarker &marker,
                                  const QString &node) const
{
    writer.writeStartElement(node); // xdr:from or xdr:to
    writer.writeTextElement(QStringLiteral("xdr:col"), QString::number(marker.col()));
    writer.writeTextElement(QStringLiteral("xdr:colOff"), QString::number(marker.colOff()));
    writer.writeTextElement(QStringLiteral("xdr:row"), QString::number(marker.row()));
    writer.writeTextElement(QStringLiteral("xdr:rowOff"), QString::number(marker.rowOff()));
    writer.writeEndElement();
}

void DrawingAnchor::saveXmlObject(QXmlStreamWriter &writer) const
{
    if (m_objectType == Picture)
        saveXmlObjectPicture(writer);
    else if (m_objectType == ConnectionShape)
        saveXmlObjectConnectionShape(writer);
    else if (m_objectType == GraphicFrame)
        saveXmlObjectGraphicFrame(writer);
    else if (m_objectType == GroupShape)
        saveXmlObjectGroupShape(writer);
    else if (m_objectType == Shape)
        saveXmlObjectShape(writer);
}

void DrawingAnchor::saveXmlObjectConnectionShape(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:cxnSp")); ///?
    writer.writeAttribute(QStringLiteral("macro"), cxnSp_macro);

    writer.writeStartElement(QStringLiteral("xdr:nvCxnSpPr"));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), xsp_cNvPR_id);
    writer.writeAttribute(QStringLiteral("name"), xsp_cNvPR_name);
    writer.writeEmptyElement(QStringLiteral("xdr:cNvCxnSpPr"));
    writer.writeEndElement(); // xdr:nvCxnSpPr

    writer.writeStartElement(QStringLiteral("xdr:spPr"));
    if (!xbwMode.isNull()) {
        writer.writeAttribute(QStringLiteral("bwMode"), xbwMode);
    }

    writer.writeStartElement(QStringLiteral("a:xfrm"));
    if (!cxnSp_filpV.isEmpty()) {
        writer.writeAttribute(QStringLiteral("flipV"), cxnSp_filpV);
    }
    writer.writeEmptyElement(QStringLiteral("a:off"));
    writer.writeAttribute(QStringLiteral("x"), QString::number(posTA.x()));
    writer.writeAttribute(QStringLiteral("y"), QString::number(posTA.y()));
    writer.writeEmptyElement(QStringLiteral("a:ext"));
    writer.writeAttribute(QStringLiteral("cx"), QString::number(extTA.width()));
    writer.writeAttribute(QStringLiteral("cy"), QString::number(extTA.height()));
    writer.writeEndElement(); // a:xfrm

    writer.writeStartElement(QStringLiteral("a:prstGeom"));
    writer.writeAttribute(QStringLiteral("prst"), xprstGeom_prst);
    writer.writeEmptyElement(QStringLiteral("a:avLst"));
    writer.writeEndElement(); // a:prstGeom

    // ln（有任意属性才写）
    const bool hasLn =
        !xIn_w.isEmpty() || !xIn_cap.isEmpty() || !xIn_cmpd.isEmpty() || !xIn_algn.isEmpty() ||
        !x_headEnd_tyep.isEmpty() || !x_headEnd_w.isEmpty() || !x_headEnd_len.isEmpty() ||
        !x_tailEnd_tyep.isEmpty() || !x_tailEnd_w.isEmpty() || !x_tailEnd_len.isEmpty();
    if (hasLn) {
        writer.writeStartElement(QStringLiteral("a:ln"));
        if (!xIn_w.isEmpty())
            writer.writeAttribute(QStringLiteral("w"), xIn_w);
        if (!xIn_cap.isEmpty())
            writer.writeAttribute(QStringLiteral("cap"), xIn_cap);
        if (!xIn_cmpd.isEmpty())
            writer.writeAttribute(QStringLiteral("cmpd"), xIn_cmpd);
        if (!xIn_algn.isEmpty())
            writer.writeAttribute(QStringLiteral("algn"), xIn_algn);

        if (!x_headEnd_tyep.isEmpty() || !x_headEnd_w.isEmpty() || !x_headEnd_len.isEmpty()) {
            writer.writeEmptyElement(QStringLiteral("a:headEnd"));
            if (!x_headEnd_tyep.isEmpty())
                writer.writeAttribute(QStringLiteral("type"), x_headEnd_tyep);
            if (!x_headEnd_w.isEmpty())
                writer.writeAttribute(QStringLiteral("w"), x_headEnd_w);
            if (!x_headEnd_len.isEmpty())
                writer.writeAttribute(QStringLiteral("len"), x_headEnd_len);
        }
        if (!x_tailEnd_tyep.isEmpty() || !x_tailEnd_w.isEmpty() || !x_tailEnd_len.isEmpty()) {
            writer.writeEmptyElement(QStringLiteral("a:tailEnd"));
            if (!x_tailEnd_tyep.isEmpty())
                writer.writeAttribute(QStringLiteral("type"), x_tailEnd_tyep);
            if (!x_tailEnd_w.isEmpty())
                writer.writeAttribute(QStringLiteral("w"), x_tailEnd_w);
            if (!x_tailEnd_len.isEmpty())
                writer.writeAttribute(QStringLiteral("len"), x_tailEnd_len);
        }
        writer.writeEndElement(); // a:ln
    }

    writer.writeEndElement(); // xdr:spPr

    // ---- style（至少有一个 ref idx 非空才写） ----
    const bool hasStyle = !Style_inref_idx.isEmpty() || !style_fillref_idx.isEmpty() ||
                          !style_effectref_idx.isEmpty() || !style_forntref_idx.isEmpty();
    if (hasStyle) {
        writer.writeStartElement(QStringLiteral("xdr:style"));

        auto writeRef = [&](const QString &refName, const QString &idx, const QString &val) {
            if (idx.isEmpty())
                return;
            writer.writeStartElement(refName);
            writer.writeAttribute(QStringLiteral("idx"), idx);
            if (!val.isEmpty()) {
                writer.writeStartElement(QStringLiteral("a:schemeClr"));
                writer.writeAttribute(QStringLiteral("val"), val);
                writer.writeEndElement();
            }
            writer.writeEndElement();
        };
        writeRef(QStringLiteral("a:lnRef"), Style_inref_idx, Style_inref_val);
        writeRef(QStringLiteral("a:fillRef"), style_fillref_idx, style_fillref_val);
        writeRef(QStringLiteral("a:effectRef"), style_effectref_idx, style_effectref_val);
        writeRef(QStringLiteral("a:fontRef"), style_forntref_idx, style_forntref_val);

        writer.writeEndElement(); // xdr:style
    }

    writer.writeEndElement(); // xdr:cxnSp
}

void DrawingAnchor::saveXmlObjectGraphicFrame(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:graphicFrame"));
    writer.writeAttribute(QStringLiteral("macro"), QString());

    writer.writeStartElement(QStringLiteral("xdr:nvGraphicFramePr"));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), QString::number(m_id));
    writer.writeAttribute(QStringLiteral("name"), QStringLiteral("Chart %1").arg(m_id));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvGraphicFramePr"));
    writer.writeEndElement(); // xdr:nvGraphicFramePr

    writer.writeStartElement(QStringLiteral("xdr:xfrm"));
    writer.writeEndElement(); // xdr:xfrm

    writer.writeStartElement(QStringLiteral("a:graphic"));
    writer.writeStartElement(QStringLiteral("a:graphicData"));
    writer.writeAttribute(QStringLiteral("uri"),
                          QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"));

    int idx = m_drawing->workbook->chartFiles().indexOf(m_chartFile);
    m_drawing->relationships()->addDocumentRelationship(
        QStringLiteral("/chart"), QStringLiteral("../charts/chart%1.xml").arg(idx + 1));

    writer.writeEmptyElement(QStringLiteral("c:chart"));
    writer.writeAttribute(QStringLiteral("xmlns:c"),
                          QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"));
    writer.writeAttribute(
        QStringLiteral("xmlns:r"),
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"));
    writer.writeAttribute(QStringLiteral("r:id"),
                          QStringLiteral("rId%1").arg(m_drawing->relationships()->count()));

    writer.writeEndElement(); // a:graphicData
    writer.writeEndElement(); // a:graphic
    writer.writeEndElement(); // xdr:graphicFrame
}

void DrawingAnchor::saveXmlObjectGroupShape(QXmlStreamWriter &writer) const
{
    Q_UNUSED(writer)
}

void DrawingAnchor::saveXmlObjectPicture(QXmlStreamWriter &writer) const
{
    Q_ASSERT(m_objectType == Picture && m_pictureFile);

    writer.writeStartElement(QStringLiteral("xdr:pic"));

    writer.writeStartElement(QStringLiteral("xdr:nvPicPr"));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), QString::number(m_id + 1)); // liufeijin
    writer.writeAttribute(QStringLiteral("name"), QStringLiteral("Picture %1").arg(m_id));

    writer.writeStartElement(QStringLiteral("xdr:cNvPicPr"));
    writer.writeEmptyElement(QStringLiteral("a:picLocks"));
    writer.writeAttribute(QStringLiteral("noChangeAspect"), QStringLiteral("1"));
    writer.writeEndElement(); // xdr:cNvPicPr

    writer.writeEndElement(); // xdr:nvPicPr

    m_drawing->relationships()->addDocumentRelationship(QStringLiteral("/image"),
                                                        QStringLiteral("../media/image%1.%2")
                                                            .arg(m_pictureFile->index() + 1)
                                                            .arg(m_pictureFile->suffix()));

    writer.writeStartElement(QStringLiteral("xdr:blipFill"));
    writer.writeEmptyElement(QStringLiteral("a:blip"));
    writer.writeAttribute(
        QStringLiteral("xmlns:r"),
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"));
    writer.writeAttribute(QStringLiteral("r:embed"),
                          QStringLiteral("rId%1").arg(m_drawing->relationships()->count()));
    writer.writeStartElement(QStringLiteral("a:stretch"));
    writer.writeEmptyElement(QStringLiteral("a:fillRect"));
    writer.writeEndElement(); // a:stretch
    writer.writeEndElement(); // xdr:blipFill

    writer.writeStartElement(QStringLiteral("xdr:spPr"));

    writer.writeStartElement(QStringLiteral("a:prstGeom"));
    writer.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    writer.writeEmptyElement(QStringLiteral("a:avLst"));
    writer.writeEndElement(); // a:prstGeom

    writer.writeEndElement(); // xdr:spPr

    writer.writeEndElement(); // xdr:pic
}

int DrawingAnchor::getm_id()
{
    return (this->m_id);
}

void DrawingAnchor::saveXmlObjectShape(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:sp")); // xdr:sp
    if (!sp_macro.isEmpty())
        writer.writeAttribute(QStringLiteral("macro"), sp_macro);
    if (!sp_textlink.isEmpty())
        writer.writeAttribute(QStringLiteral("textlink"), sp_textlink);

    // ---- nvSpPr ----
    writer.writeStartElement(QStringLiteral("xdr:nvSpPr"));

    writer.writeStartElement(QStringLiteral("xdr:cNvPr"));
    if (!xsp_cNvPR_id.isEmpty())
        writer.writeAttribute(QStringLiteral("id"), xsp_cNvPR_id);
    if (!xsp_cNvPR_name.isEmpty())
        writer.writeAttribute(QStringLiteral("name"), xsp_cNvPR_name);
    writer.writeEndElement(); // xdr:cNvPr

    writer.writeEmptyElement(QStringLiteral("xdr:cNvSpPr"));
    if (!sp_txBox.isEmpty())
        writer.writeAttribute(QStringLiteral("txBox"), sp_txBox);

    writer.writeEndElement(); // xdr:nvSpPr

    // ---- spPr ----
    writer.writeStartElement(QStringLiteral("xdr:spPr"));
    if (!xbwMode.isNull()) {
        writer.writeAttribute(QStringLiteral("bwMode"), xbwMode);
    }

    // xfrm
    writer.writeStartElement(QStringLiteral("a:xfrm"));
    if (!cxnSp_filpV.isEmpty())
        writer.writeAttribute(QStringLiteral("flipV"), cxnSp_filpV);
    writer.writeEmptyElement(QStringLiteral("a:off"));
    writer.writeAttribute(QStringLiteral("x"), QString::number(posTA.x()));
    writer.writeAttribute(QStringLiteral("y"), QString::number(posTA.y()));
    writer.writeEmptyElement(QStringLiteral("a:ext"));
    writer.writeAttribute(QStringLiteral("cx"), QString::number(extTA.width()));
    writer.writeAttribute(QStringLiteral("cy"), QString::number(extTA.height()));
    writer.writeEndElement(); // a:xfrm

    // prstGeom
    if (!xprstGeom_prst.isEmpty()) {
        writer.writeStartElement(QStringLiteral("a:prstGeom"));
        writer.writeAttribute(QStringLiteral("prst"), xprstGeom_prst);
        writer.writeEmptyElement(QStringLiteral("a:avLst"));
        writer.writeEndElement(); // a:prstGeom
    }

    // blipFill（仅当该形状关联了图片）
    if (m_pictureFile) {
        m_drawing->relationships()->addDocumentRelationship(
            QStringLiteral("/image"),
            QStringLiteral("../media/image%1.%2")
                .arg(m_pictureFile->index() + 1)
                .arg(m_pictureFile->suffix()));
        writer.writeStartElement(QStringLiteral("a:blipFill"));
        if (dpiTA)
            writer.writeAttribute(QStringLiteral("dpi"), QString::number(dpiTA));
        if (rotWithShapeTA)
            writer.writeAttribute(QStringLiteral("rotWithShape"), QString::number(rotWithShapeTA));

        writer.writeStartElement(QStringLiteral("a:blip"));
        writer.writeAttribute(
            QStringLiteral("r:embed"),
            QStringLiteral("rId%1").arg(m_drawing->relationships()->count()));
        writer.writeAttribute(
            QStringLiteral("xmlns:r"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"));
        if (!sp_blip_cstate.isNull())
            writer.writeAttribute(QStringLiteral("cstate"), sp_blip_cstate);
        writer.writeEndElement(); // a:blip
        writer.writeEmptyElement(QStringLiteral("a:srcRect"));
        writer.writeStartElement(QStringLiteral("a:stretch"));
        writer.writeEmptyElement(QStringLiteral("a:fillRect"));
        writer.writeEndElement(); // a:stretch
        writer.writeEndElement(); // a:blipFill
    }

    // ln（有任意属性才写）
    const bool hasLn =
        !xIn_w.isEmpty() || !xIn_cap.isEmpty() || !xIn_cmpd.isEmpty() || !xIn_algn.isEmpty() ||
        !x_headEnd_tyep.isEmpty() || !x_headEnd_w.isEmpty() || !x_headEnd_len.isEmpty() ||
        !x_tailEnd_tyep.isEmpty() || !x_tailEnd_w.isEmpty() || !x_tailEnd_len.isEmpty();
    if (hasLn) {
        writer.writeStartElement(QStringLiteral("a:ln"));
        if (!xIn_w.isEmpty())
            writer.writeAttribute(QStringLiteral("w"), xIn_w);
        if (!xIn_cap.isEmpty())
            writer.writeAttribute(QStringLiteral("cap"), xIn_cap);
        if (!xIn_cmpd.isEmpty())
            writer.writeAttribute(QStringLiteral("cmpd"), xIn_cmpd);
        if (!xIn_algn.isEmpty())
            writer.writeAttribute(QStringLiteral("algn"), xIn_algn);

        if (!x_headEnd_tyep.isEmpty() || !x_headEnd_w.isEmpty() || !x_headEnd_len.isEmpty()) {
            writer.writeEmptyElement(QStringLiteral("a:headEnd"));
            if (!x_headEnd_tyep.isEmpty())
                writer.writeAttribute(QStringLiteral("type"), x_headEnd_tyep);
            if (!x_headEnd_w.isEmpty())
                writer.writeAttribute(QStringLiteral("w"), x_headEnd_w);
            if (!x_headEnd_len.isEmpty())
                writer.writeAttribute(QStringLiteral("len"), x_headEnd_len);
        }
        if (!x_tailEnd_tyep.isEmpty() || !x_tailEnd_w.isEmpty() || !x_tailEnd_len.isEmpty()) {
            writer.writeEmptyElement(QStringLiteral("a:tailEnd"));
            if (!x_tailEnd_tyep.isEmpty())
                writer.writeAttribute(QStringLiteral("type"), x_tailEnd_tyep);
            if (!x_tailEnd_w.isEmpty())
                writer.writeAttribute(QStringLiteral("w"), x_tailEnd_w);
            if (!x_tailEnd_len.isEmpty())
                writer.writeAttribute(QStringLiteral("len"), x_tailEnd_len);
        }
        writer.writeEndElement(); // a:ln
    }

    writer.writeEndElement(); // xdr:spPr

    // ---- style（至少有一个 ref idx 非空才写） ----
    const bool hasStyle = !Style_inref_idx.isEmpty() || !style_fillref_idx.isEmpty() ||
                          !style_effectref_idx.isEmpty() || !style_forntref_idx.isEmpty();
    if (hasStyle) {
        writer.writeStartElement(QStringLiteral("xdr:style"));

        auto writeRef = [&](const QString &refName, const QString &idx, const QString &val) {
            if (idx.isEmpty())
                return;
            writer.writeStartElement(refName);
            writer.writeAttribute(QStringLiteral("idx"), idx);
            if (!val.isEmpty()) {
                writer.writeStartElement(QStringLiteral("a:schemeClr"));
                writer.writeAttribute(QStringLiteral("val"), val);
                writer.writeEndElement();
            }
            writer.writeEndElement();
        };
        writeRef(QStringLiteral("a:lnRef"), Style_inref_idx, Style_inref_val);
        writeRef(QStringLiteral("a:fillRef"), style_fillref_idx, style_fillref_val);
        writeRef(QStringLiteral("a:effectRef"), style_effectref_idx, style_effectref_val);
        writeRef(QStringLiteral("a:fontRef"), style_forntref_idx, style_forntref_val);

        writer.writeEndElement(); // xdr:style
    }

    // ---- txBody（原样回写 load 时整段保存的 XML 子树） ----
    if (!sp_txBodyXml.isEmpty()) {
        // Qt 5 没有 writeRawData，直接通过底层 device 写原始字节，避免转义
        if (QIODevice *dev = writer.device()) {
            dev->write(sp_txBodyXml.toUtf8());
        }
    }

    writer.writeEndElement(); // xdr:sp
}

// absolute anchor

DrawingAbsoluteAnchor::DrawingAbsoluteAnchor(Drawing *drawing, ObjectType objectType)
    : DrawingAnchor(drawing, objectType)
{
}

// check point
bool DrawingAbsoluteAnchor::loadFromXml(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("absoluteAnchor"));

    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("pos")) {
                pos = loadXmlPos(reader);
            } else if (reader.name() == QLatin1String("ext")) {
                ext = loadXmlExt(reader);
            } else {
                loadXmlObject(reader);
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("absoluteAnchor")) {
            break;
        }
    }
    return true;
}

void DrawingAbsoluteAnchor::saveToXml(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:absoluteAnchor"));
    saveXmlPos(writer, pos);
    saveXmlExt(writer, ext);

    saveXmlObject(writer);

    writer.writeEmptyElement(QStringLiteral("xdr:clientData"));
    writer.writeEndElement(); // xdr:absoluteAnchor
}

// one cell anchor

DrawingOneCellAnchor::DrawingOneCellAnchor(Drawing *drawing, ObjectType objectType)
    : DrawingAnchor(drawing, objectType)
{
}

int DrawingOneCellAnchor::row() const
{
    return from.row();
}

int DrawingOneCellAnchor::col() const
{
    return from.col();
}

// check point
bool DrawingOneCellAnchor::loadFromXml(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("oneCellAnchor"));

    while (!reader.atEnd()) {
        reader.readNextStartElement();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("from")) {
                from = loadXmlMarker(reader, QLatin1String("from"));
            } else if (reader.name() == QLatin1String("ext")) {
                ext = loadXmlExt(reader);
            } else {
                loadXmlObject(reader);
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("oneCellAnchor")) {
            break;
        }
    }
    return true;
}

void DrawingOneCellAnchor::saveToXml(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:oneCellAnchor"));

    saveXmlMarker(writer, from, QStringLiteral("xdr:from"));
    saveXmlExt(writer, ext);

    saveXmlObject(writer);

    writer.writeEmptyElement(QStringLiteral("xdr:clientData"));
    writer.writeEndElement(); // xdr:oneCellAnchor
}

/*
   Two cell anchor

   This class specifies a two cell anchor placeholder for a group
   , a shape, or a drawing element. It moves with
   cells and its extents are in EMU units.
*/
DrawingTwoCellAnchor::DrawingTwoCellAnchor(Drawing *drawing, ObjectType objectType)
    : DrawingAnchor(drawing, objectType)
{
}

int DrawingTwoCellAnchor::row() const
{
    return from.row();
}

int DrawingTwoCellAnchor::col() const
{
    return from.col();
}

// check point
bool DrawingTwoCellAnchor::loadFromXml(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("twoCellAnchor"));

    /*
    <xsd:complexType name="CT_TwoCellAnchor">
        <xsd:sequence>
            <xsd:element name="from" type="CT_Marker"/>
            <xsd:element name="to" type="CT_Marker"/>
            <xsd:group ref="EG_ObjectChoices"/>
            <xsd:element name="clientData" type="CT_AnchorClientData" minOccurs="1" maxOccurs="1"/>
        </xsd:sequence>
        <xsd:attribute name="editAs" type="ST_EditAs" use="optional" default="twoCell"/>
    </xsd:complexType>
    */

    //{{ liufeijin
    QXmlStreamAttributes attrs =
        reader.attributes(); // for absolute twocell added by liufeijin 20181024
    editASName = attrs.value(QLatin1String("editAs")).toString();
    //}}

    while (!reader.atEnd()) {
        reader.readNextStartElement();

        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QLatin1String("from")) {
                from = loadXmlMarker(reader, QLatin1String("from"));
            } else if (reader.name() == QLatin1String("to")) {
                to = loadXmlMarker(reader, QLatin1String("to"));
            } else if (reader.name() == QLatin1String("clientData")) {
                // clientData
            } else {
                /*
                <xsd:group name="EG_ObjectChoices">
                    <xsd:sequence>
                        <xsd:choice minOccurs="1" maxOccurs="1">
                            <xsd:element name="sp" type="CT_Shape"/>
                            <xsd:element name="grpSp" type="CT_GroupShape"/>
                            <xsd:element name="graphicFrame" type="CT_GraphicalObjectFrame"/>
                            <xsd:element name="cxnSp" type="CT_Connector"/>
                            <xsd:element name="pic" type="CT_Picture"/>
                            <xsd:element name="contentPart" type="CT_Rel"/>
                        </xsd:choice>
                    </xsd:sequence>
                </xsd:group>
                */

                loadXmlObject(reader);
            }
        } else if (reader.tokenType() == QXmlStreamReader::EndElement &&
                   reader.name() == QLatin1String("twoCellAnchor")) {
            break;
        }
    }
    return true;
}

void DrawingTwoCellAnchor::saveToXml(QXmlStreamWriter &writer) const
{
    writer.writeStartElement(QStringLiteral("xdr:twoCellAnchor"));

    //{{ liufeijin
    // writer.writeAttribute(QStringLiteral("editAs"), QStringLiteral("oneCell"));
    if (!editASName.isNull()) {
        writer.writeAttribute(QStringLiteral("editAs"), editASName); // QStringLiteral("oneCell")
    }
    // }}

    saveXmlMarker(writer, from, QStringLiteral("xdr:from"));
    saveXmlMarker(writer, to, QStringLiteral("xdr:to"));

    saveXmlObject(writer);

    writer.writeEmptyElement(QStringLiteral("xdr:clientData"));
    writer.writeEndElement(); // xdr:twoCellAnchor
}

QT_END_NAMESPACE_XLSX
